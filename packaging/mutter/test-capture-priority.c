#include <glib.h>

#define REDRAW_PRIORITY (G_PRIORITY_HIGH_IDLE + 50)
#define LEGACY_CAPTURE_PRIORITY (REDRAW_PRIORITY + 1)
#define DEADLINE_MS 50

typedef struct
{
  GMainLoop *loop;
  gboolean capture_ran;
} TestState;

typedef struct
{
  gboolean capture_ran;
  gint64 elapsed_us;
} TestResult;

static gboolean
keep_competing_source_ready (gpointer user_data)
{
  (void) user_data;

  return G_SOURCE_CONTINUE;
}

static gboolean
capture_cb (gpointer user_data)
{
  TestState *state = user_data;

  state->capture_ran = TRUE;
  g_main_loop_quit (state->loop);

  return G_SOURCE_REMOVE;
}

static gboolean
deadline_cb (gpointer user_data)
{
  TestState *state = user_data;

  g_main_loop_quit (state->loop);

  return G_SOURCE_REMOVE;
}

static GSource *
attach_source (GMainContext *context,
               GSource      *source,
               int           priority,
               GSourceFunc   callback,
               gpointer      user_data)
{
  g_source_set_priority (source, priority);
  g_source_set_callback (source, callback, user_data, NULL);
  g_source_attach (source, context);

  return source;
}

static TestResult
run_case (int competing_priority,
          int capture_priority)
{
  GMainContext *context = g_main_context_new ();
  GMainLoop *loop = g_main_loop_new (context, FALSE);
  TestState state = { .loop = loop };
  GSource *redraw_source;
  GSource *capture_source;
  GSource *deadline_source;
  gint64 started_us;
  TestResult result;

  redraw_source = attach_source (context,
                                 g_idle_source_new (),
                                 competing_priority,
                                 keep_competing_source_ready,
                                 NULL);
  capture_source = attach_source (context,
                                  g_timeout_source_new (1),
                                  capture_priority,
                                  capture_cb,
                                  &state);
  deadline_source = attach_source (context,
                                   g_timeout_source_new (DEADLINE_MS),
                                   G_PRIORITY_HIGH,
                                   deadline_cb,
                                   &state);

  started_us = g_get_monotonic_time ();
  g_main_loop_run (loop);
  result.capture_ran = state.capture_ran;
  result.elapsed_us = g_get_monotonic_time () - started_us;

  g_source_destroy (deadline_source);
  g_source_destroy (capture_source);
  g_source_destroy (redraw_source);
  g_source_unref (deadline_source);
  g_source_unref (capture_source);
  g_source_unref (redraw_source);
  g_main_loop_unref (loop);
  g_main_context_unref (context);

  return result;
}

int
main (void)
{
  TestResult legacy = run_case (REDRAW_PRIORITY,
                                LEGACY_CAPTURE_PRIORITY);
  TestResult candidate = run_case (REDRAW_PRIORITY,
                                   G_PRIORITY_DEFAULT);
  TestResult same_priority = run_case (G_PRIORITY_DEFAULT,
                                       G_PRIORITY_DEFAULT);

  g_print ("legacy_priority=%d capture_ran=%s elapsed_ms=%.3f\n",
           LEGACY_CAPTURE_PRIORITY,
           legacy.capture_ran ? "true" : "false",
           legacy.elapsed_us / 1000.0);
  g_print ("candidate_priority=%d capture_ran=%s elapsed_ms=%.3f\n",
           G_PRIORITY_DEFAULT,
           candidate.capture_ran ? "true" : "false",
           candidate.elapsed_us / 1000.0);
  g_print ("same_priority=%d capture_ran=%s elapsed_ms=%.3f\n",
           G_PRIORITY_DEFAULT,
           same_priority.capture_ran ? "true" : "false",
           same_priority.elapsed_us / 1000.0);

  if (legacy.capture_ran)
    {
      g_printerr ("FAIL: priority 151 was not starved by priority 150\n");
      return 1;
    }

  if (!candidate.capture_ran)
    {
      g_printerr ("FAIL: priority 0 capture missed the 50 ms deadline\n");
      return 1;
    }

  if (!same_priority.capture_ran)
    {
      g_printerr ("FAIL: priority 0 capture was starved by a priority 0 peer\n");
      return 1;
    }

  g_print ("PASS: default priority eliminates redraw starvation and shares dispatch fairly\n");
  return 0;
}
