using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Threading;

namespace lollipop;

internal static class UpdateNotificationTests {
  private static void Check(bool condition, string message) {
    if (!condition)
      throw new Exception(message);
  }

  private static void Wait(Task task) {
    var deadline = DateTime.UtcNow.AddSeconds(15);
    while (!task.IsCompleted && DateTime.UtcNow < deadline) {
      var frame = new DispatcherFrame();
      Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.Background,
                                               new Action(() => frame.Continue = false));
      Dispatcher.PushFrame(frame);
      Thread.Sleep(15);
    }
    Check(task.IsCompleted, "The background update check did not finish");
    task.GetAwaiter().GetResult();
  }

  internal static void Run(string sandbox) {
    SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
    var launcher = new Launcher(sandbox, Environment.ProcessPath!,
                                Path.Combine(sandbox, "update-notification-location.txt"));
    using var finish = new ManualResetEventSlim();
    try {
      var notice = launcher.Find<Border>("UpdateNotice");
      var badge = launcher.Find<System.Windows.Shapes.Ellipse>("UpdateBadge");
      Check(notice.Visibility == Visibility.Collapsed && badge.Visibility == Visibility.Collapsed,
            "The launcher advertised an update before checking");
      Wait(launcher.CheckUpdates(() => throw new IOException("Offline startup fixture")));
      Check(notice.Visibility == Visibility.Collapsed && badge.Visibility == Visibility.Collapsed &&
                launcher.CurrentPage == "Home" && launcher.Find<Button>("Primary").IsEnabled,
            "An offline startup advertised an update or interrupted launch");
      var release = new Release { Tag = "v99.0.0", Body = "New release notes" };
      var checks = 0;
      var pending = launcher.CheckUpdates(() => {
        Interlocked.Increment(ref checks);
        if (!finish.Wait(TimeSpan.FromSeconds(15)))
          throw new IOException("The release fixture timed out");
        return release;
      });
      Check(launcher.CheckingUpdates && !pending.IsCompleted && !launcher.Busy &&
                launcher.Find<Button>("Primary").IsEnabled &&
                !launcher.Find<Button>("CheckUpdates").IsEnabled &&
                !launcher.Find<Button>("UpdateLauncher").IsEnabled,
            "A background check blocked launch or allowed overlapping update actions");
      Wait(launcher.CheckUpdates(() => throw new Exception("Duplicate release request")));
      launcher.SetPage("Settings");
      launcher.Busy = true;
      launcher.Show("Busy", "Working", "Fixture operation", null, null, null);
      finish.Set();
      Wait(pending);
      Check(checks == 1 && !launcher.CheckingUpdates && launcher.Busy &&
                launcher.CurrentPage == "Settings" &&
                !launcher.Find<Button>("CheckUpdates").IsEnabled &&
                !launcher.Find<Button>("UpdateLauncher").IsEnabled &&
                !launcher.Find<Button>("UpdateNoticeAction").IsEnabled,
            "Finishing a release check changed pages or re-enabled busy actions");
      Check(notice.Visibility == Visibility.Visible && badge.Visibility == Visibility.Visible &&
                launcher.Find<TextBlock>("UpdateNoticeText").Text.Contains(release.Tag) &&
                AutomationProperties.GetName(launcher.Find<Button>("UpdatesNav"))
                    .Contains(release.Tag),
            "A newer stable release did not show its notice and badge");
      var notes = launcher.Find<RichTextBox>("ReleaseNotes").Document;
      Check(new TextRange(notes.ContentStart, notes.ContentEnd).Text.Trim() == release.Body,
            "The background check lost the release notes");
      launcher.Busy = false;
      launcher.Home();
      launcher.Find<Button>("UpdateNoticeAction")
          .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
      Check(launcher.CurrentPage == "Updates", "The update notice did not open Updates");
      launcher.Find<Button>("DismissUpdateNotice")
          .RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
      launcher.SetPage("Home");
      launcher.ShowRelease(release);
      Check(notice.Visibility == Visibility.Collapsed && badge.Visibility == Visibility.Visible,
            "Dismissing an update lost its badge or the same release showed the notice again");
      Wait(launcher.CheckUpdates(() => throw new IOException("Offline fixture")));
      Check(!launcher.CheckingUpdates && launcher.CurrentPage == "Home" &&
                launcher.Find<Button>("Primary").IsEnabled &&
                launcher.Find<Button>("CheckUpdates").IsEnabled &&
                launcher.Find<Button>("UpdateLauncher").IsEnabled &&
                launcher.Find<TextBlock>("UpdateStatus").Text == "Couldn't check for updates." &&
                badge.Visibility == Visibility.Visible,
            "An offline check interrupted launch, lost a known update or prevented retry");
      Wait(launcher.CheckUpdates(() => new Release { Tag = "v99.0.1" }));
      Check(notice.Visibility == Visibility.Visible,
            "Dismissing one release also hid a later release");
      foreach (var current in new Release?[] {
                 null, new() { Tag = "v" + Releases.GameVersion.Split('-', '+')[0] },
                 new() { Tag = "v0.0.0" }
               }) {
        Wait(launcher.CheckUpdates(() => current));
        Check(notice.Visibility == Visibility.Collapsed && badge.Visibility == Visibility.Collapsed,
              "A missing, current or older release was advertised as an update");
      }
      launcher.Busy = true;
      Wait(launcher.CheckUpdates(() => throw new Exception("Release request while busy")));
      Check(!launcher.CheckingUpdates, "A busy launcher started another release check");
    } finally {
      finish.Set();
      launcher.Busy = false;
      launcher.Window.Close();
    }
  }
}
