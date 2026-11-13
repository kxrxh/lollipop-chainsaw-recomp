using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows;

namespace lollipop;

public partial class App : Application {
  private Mutex? instanceLock;

  [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
  private static extern int SetCurrentProcessExplicitAppUserModelID(string appId);

  internal static void SetTaskbarIdentity() {
    Marshal.ThrowExceptionForHR(
        SetCurrentProcessExplicitAppUserModelID("LollipopChainsaw.Recomp.Launcher"));
  }

  protected override void OnStartup(StartupEventArgs e) {
    base.OnStartup(e);
    try {
      SetTaskbarIdentity();
      var executable = Environment.ProcessPath ?? throw new InvalidOperationException(
                                                      "Could not locate the launcher executable.");
      instanceLock =
          new Mutex(true, "Local\\LollipopSetup-" + Tools.HashText(executable.ToUpperInvariant()),
                    out var created);
      if (!created)
        throw new InvalidOperationException("This launcher is already open.");
      MainWindow = new Launcher(LauncherPackage.ExtractKit(), executable).Window;
      MainWindow.Show();
    } catch (Exception error) {
      MessageBox.Show(error.Message, "Lollipop Chainsaw Launcher", MessageBoxButton.OK,
                      MessageBoxImage.Error);
      Shutdown(1);
    }
  }

  protected override void OnExit(ExitEventArgs e) {
    instanceLock?.Dispose();
    base.OnExit(e);
  }
}
