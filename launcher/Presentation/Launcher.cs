using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace lollipop;

internal sealed class Launcher {
  internal readonly Window Window;
  internal string? Source, Destination;
  internal string Screen = "";
  internal string CurrentPage = "Home";
  internal bool Busy, CheckingUpdates;
  internal readonly GraphicsSettings Settings;
  private readonly string kit, executable;
  private readonly DispatcherTimer timer = new DispatcherTimer();
  private readonly Stopwatch elapsed = new Stopwatch();
  private string stepLabel = "";
  private Release? latest;
  private IInputElement? helpReturnFocus;

  internal Launcher(string kit, string executable) {
    this.kit = kit;
    this.executable = executable;
    Window = new MainWindow();
    FitWorkArea(SystemParameters.WorkArea.Size);
    Window.Icon = Image("lollipop.png");
    Find<System.Windows.Controls.Image>("BrandIcon").Source = Window.Icon;
    Find<System.Windows.Controls.Image>("GameTitle").Source = Image("title-logo.png");
    Find<ImageBrush>("Cover").ImageSource = Image("hero.png");
    Find<TextBlock>("RecompVersion").Text = Releases.GameVersion;
    Find<TextBlock>("LauncherVersion").Text = "Launcher version: " + Releases.LauncherVersion;
    Find<TextBlock>("BundledVersion").Text = "Bundled recomp version: " + Releases.GameVersion;
    Find<TextBlock>("RepairDescription").Text =
        "Build recomp " + Releases.GameVersion +
        " from this launcher. Saves and settings are kept.";
    Find<RichTextBox>("ReleaseNotes").Document =
        MarkdownNotes.Render(Releases.Resource("CHANGELOG.md"), uri => OpenPath(uri.AbsoluteUri));
    Find<Button>("Minimize").Click += delegate { Window.WindowState = WindowState.Minimized; };
    Find<Button>("Maximize").Click += delegate {
      Window.WindowState =
          Window.WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    };
    Find<Button>("CloseWindow").Click += delegate { Window.Close(); };
    Find<Button>("HomeNav").Click += delegate { SetPage("Home"); };
    Find<Button>("ManageNav").Click += delegate { SetPage("Manage"); };
    Find<Button>("SettingsNav").Click += delegate { SetPage("Settings"); };
    Find<Button>("UpdatesNav").Click += delegate { SetPage("Updates"); };
    Find<Button>("ManageShortcut").Click += delegate { SetPage("Manage"); };
    Find<Button>("SettingsShortcut").Click += delegate { SetPage("Settings"); };
    Find<Button>("UpdatesShortcut").Click += delegate { SetPage("Updates"); };
    Find<Button>("ChooseIso").Click += delegate {
      var picker =
          new Microsoft.Win32.OpenFileDialog { Title = "Choose your Xbox 360 disc image",
                                               Filter = "Xbox 360 disc image (*.iso)|*.iso" };
      if (picker.ShowDialog(Window) == true) {
        Source = picker.FileName;
        Home();
      }
    };
    Find<Button>("ChooseFolder").Click += delegate { ChooseDestination(); };
    Find<Button>("Primary").Click += delegate {
      if (Screen == "Failure")
        Home();
      else if (Screen == "Install" && CurrentPage != "Manage")
        SetPage("Manage");
      else
        Start(Screen == "Ready", false, Screen == "Update");
    };
    Find<Button>("Precompile").Click += delegate { Start(true, true); };
    Find<Button>("Update").Click += delegate { Start(false, false, true); };
    Find<Button>("Secondary").Click += delegate {
      if (Screen == "Failure")
        OpenPath(LogPath);
      else
        ChooseDestination();
    };
    Find<Button>("OpenInstall").Click += delegate { OpenPath(Destination); };
    Find<Button>("OpenSaves").Click += delegate { OpenPath(Path.Combine(Destination!, "user")); };
    Find<Button>("CreateDesktopShortcut").Click += delegate { CreateDesktopShortcut(); };
    Find<Button>("CheckUpdates").Click += delegate { CheckUpdates(); };
    Find<Button>("ViewRelease").Click += delegate {
      OpenPath(latest == null ? Releases.Page
                              : Releases.Page + "/tag/" + Uri.EscapeDataString(latest.Tag));
    };
    Find<Button>("Help").Click += delegate { SetHelp(true); };
    Find<Button>("HelpClose").Click += delegate { SetHelp(false); };
    Find<Button>("HelpDone").Click += delegate { SetHelp(false); };
    Window.PreviewKeyDown += delegate(object sender, KeyEventArgs args) {
      if (args.Key == Key.Escape && Find<Border>("HelpOverlay").Visibility == Visibility.Visible) {
        SetHelp(false);
        args.Handled = true;
      }
    };
    Window.Closing += delegate(object? sender, System.ComponentModel.CancelEventArgs args) {
      if (!Busy)
        return;
      args.Cancel = true;
      MessageBox.Show(Window,
                      "Keep the launcher open until the current step finishes. " +
                          "If the game is running, close its window first.",
                      "Launcher is busy");
    };
    timer.Interval = TimeSpan.FromMilliseconds(300);
    timer.Tick += delegate {
      Find<TextBlock>("StepStatus").Text =
          stepLabel + "  /  " + elapsed.Elapsed.ToString(@"hh\:mm\:ss");
    };
    Settings = new GraphicsSettings(this);
    var ownFolder = Path.GetDirectoryName(executable);
    if (Installer.Ready(ownFolder) || GameUpdate.CanUpdate(ownFolder))
      Destination = ownFolder;
    Home();
  }

  internal T Find<T>(string name)
      where T : class {
    return Window.FindName(name) as T ??
           throw new InvalidOperationException("Missing launcher control: " + name);
  }

  internal void FitWorkArea(Size area) {
    Window.MinWidth = Math.Min(Window.MinWidth, area.Width);
    Window.MinHeight = Math.Min(Window.MinHeight, area.Height);
    Window.Width = Math.Min(Window.Width, area.Width);
    Window.Height = Math.Min(Window.Height, area.Height);
  }

  private string LogPath {
    get { return Path.Combine(Destination!, "out/launcher/latest.log"); }
  }

  private static BitmapSource Image(string name) {
    using (var stream = LauncherPackage.Resource(name)) {
      var image = BitmapFrame.Create(stream, BitmapCreateOptions.None, BitmapCacheOption.OnLoad);
      image.Freeze();
      return image;
    }
  }

  internal void SetHelp(bool open) {
    var overlay = Find<Border>("HelpOverlay");
    if ((overlay.Visibility == Visibility.Visible) == open)
      return;
    if (open)
      helpReturnFocus = Keyboard.FocusedElement ?? Find<Button>("Help");
    Find<Grid>("LauncherContent").IsEnabled = !open;
    overlay.Visibility = open ? Visibility.Visible : Visibility.Collapsed;
    RefreshPage();
    if (open) {
      Find<ScrollViewer>("HelpScroll").ScrollToTop();
      FocusManager.SetFocusedElement(Find<Border>("HelpDialog"), Find<Button>("HelpDone"));
      Find<Button>("HelpDone").Focus();
    } else {
      var target = helpReturnFocus as UIElement;
      if (target != null && target.IsEnabled && target.IsVisible)
        target.Focus();
      else
        Find<Button>("Help").Focus();
      helpReturnFocus = null;
    }
  }

  private void ChooseDestination() {
    if (Busy)
      return;
    var picker = new Microsoft.Win32.OpenFolderDialog {
      Title = "Choose an existing game folder, or an empty folder for a new installation.",
      InitialDirectory = Destination ?? ""
    };
    if (picker.ShowDialog(Window) == true) {
      Destination = picker.FolderName;
      Home();
    }
  }

  private void OpenPath(string? path) {
    try {
      if (string.IsNullOrWhiteSpace(path))
        throw new IOException("This location is not available yet.");
      Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
    } catch (Exception error) {
      MessageBox.Show(Window, error.Message, "Could not open location");
    }
  }

  internal void SetPage(string page) {
    if (Screen == "Busy")
      return;
    CurrentPage = page;
    RefreshPage();
    Find<ScrollViewer>("PageScroll").ScrollToTop();
  }

  private void RefreshPage() {
    var modal = Find<Border>("HelpOverlay").Visibility == Visibility.Visible;
    Find<Button>("Primary").IsDefault = !modal && CurrentPage != "Settings";
    Find<Button>("SaveSettings").IsDefault =
        !modal && CurrentPage == "Settings" && Screen != "Busy";
    foreach (var name in new[] { "Home", "Manage", "Settings", "Updates" }) {
      Find<FrameworkElement>(name + "Page").Visibility =
          CurrentPage == name ? Visibility.Visible : Visibility.Collapsed;
      Find<Button>(name + "Nav").Tag = CurrentPage == name ? "Selected" : null;
    }
    Find<ScrollViewer>("PageScroll").Visibility =
        CurrentPage == "Settings" ? Visibility.Collapsed : Visibility.Visible;
    foreach (var name in new[] { "Heading", "Description", "Location" })
      Find<TextBlock>(name).Visibility =
          CurrentPage == "Settings" && Screen != "Busy" && Screen != "Failure"
              ? Visibility.Collapsed
              : Visibility.Visible;
    Find<TextBlock>("PageHeading").Text = CurrentPage == "Home"       ? "Overview"
                                          : CurrentPage == "Manage"   ? "Manage game"
                                          : CurrentPage == "Settings" ? "Game settings"
                                                                      : "Updates";
    if (Screen == "Install") {
      Find<Button>("Primary").Content = CurrentPage == "Manage" ? "Install game" : "Set up game";
      Find<Button>("Primary").IsEnabled =
          CurrentPage != "Manage" || (Source != null && Destination != null);
    }
  }

  internal void Show(string state, string title, string body, string? action, string? alternative,
                     string? detail) {
    Screen = state;
    var busy = state == "Busy";
    Find<TextBlock>("Heading").Text = title;
    Find<TextBlock>("Description").Text = body;
    Find<TextBlock>("Location").Text = detail ?? "";
    Find<TextBlock>("Location").ToolTip = detail;
    Find<StackPanel>("Inputs").Visibility =
        state == "Install" ? Visibility.Visible : Visibility.Collapsed;
    Find<StackPanel>("GameActions").Visibility =
        state == "Ready" ? Visibility.Visible : Visibility.Collapsed;
    Find<CheckBox>("DesktopShortcut").IsEnabled = !busy;
    Find<CheckBox>("DesktopShortcut").Visibility =
        state is "Install" or "Update" ? Visibility.Visible : Visibility.Collapsed;
    Find<Button>("CreateDesktopShortcut").IsEnabled = state == "Ready";
    Find<TextBlock>("ShortcutStatus").Text = "";
    var precompile = Find<Button>("Precompile");
    precompile.IsEnabled =
        Destination != null &&
        File.Exists(
            Path.Combine(Destination, "out/build/win-amd64-release/shader-precompile-v1.ready"));
    precompile.ToolTip =
        precompile.IsEnabled
            ? "Prepare recorded shaders before playing to reduce compilation pauses."
            : "Apply this launcher build to enable shader preparation.";
    Find<Button>("Update").Visibility = state == "Ready" && GameUpdate.CanUpdate(Destination)
                                            ? Visibility.Visible
                                            : Visibility.Collapsed;
    foreach (var name in new[] { "ChooseIso", "ChooseFolder", "HomeNav", "ManageNav", "SettingsNav",
                                 "UpdatesNav", "ManageShortcut", "SettingsShortcut",
                                 "UpdatesShortcut" })
      Find<Button>(name).IsEnabled = !busy;
    Find<Button>("CheckUpdates").IsEnabled = !busy && !CheckingUpdates;
    Find<Button>("OpenInstall").IsEnabled = !busy && Directory.Exists(Destination);
    Find<Button>("OpenSaves").IsEnabled =
        !busy && Destination != null && Directory.Exists(Path.Combine(Destination, "user"));
    Find<TextBlock>("GameStatus").Text = state == "Ready"     ? "Ready to play"
                                         : state == "Update"  ? "Repair needed"
                                         : busy               ? "Working"
                                         : state == "Failure" ? "Needs attention"
                                                              : "Not installed";
    Find<System.Windows.Shapes.Ellipse>("StatusDot").Fill = new SolidColorBrush(
        state == "Ready" ? Color.FromRgb(127, 227, 176) : Color.FromRgb(255, 133, 186));
    Find<TextBlock>("InstalledBuildVersion").Text =
        "Installed recomp version: " +
        (state == "Install" ? "Not installed" : Releases.InstalledVersion(Destination));
    var primary = Find<Button>("Primary");
    primary.Content = action ?? "Working...";
    primary.IsEnabled = !busy;
    Find<Button>("Secondary").Content = alternative;
    Find<Button>("Secondary").Visibility =
        alternative == null ? Visibility.Collapsed : Visibility.Visible;
    Find<ProgressBar>("Progress").Visibility = busy ? Visibility.Visible : Visibility.Collapsed;
    Find<ProgressBar>("Progress").IsIndeterminate = true;
    Find<TextBlock>("StepStatus").Visibility = Find<ProgressBar>("Progress").Visibility;
    Find<TextBlock>("StepStatus").Text = "";
    Find<Expander>("ErrorLog").Visibility = Visibility.Collapsed;
    Find<TextBox>("LogText").Clear();
    Settings.Refresh(Destination, busy);
    RefreshPage();
  }

  internal void Home() {
    Find<TextBlock>("IsoName").Text =
        Source == null ? "Select an ISO file" : Path.GetFileName(Source);
    Find<TextBlock>("FolderName").Text = Destination ?? "Select a game folder";
    Find<Button>("ChooseIso").ToolTip = Source;
    Find<Button>("ChooseFolder").ToolTip = Destination;
    if (Installer.Ready(Destination)) {
      Show("Ready", "Ready when you are.", "Jump back into San Romero.", "Play", "Locate game",
           null);
    } else if (GameUpdate.CanUpdate(Destination)) {
      Show("Update", "Let's finish your installation.",
           "Repair using this launcher build. Your imported disc, saves and settings are kept.",
           "Repair game", "Locate game", null);
    } else {
      Show("Install", "Your game starts here.",
           "Install your Xbox 360 copy, or locate a game you've already installed.", "Set up game",
           "Locate game", null);
    }
  }

  internal void Progress(string activity, string status, int percent) {
    var bar = Find<ProgressBar>("Progress");
    bar.IsIndeterminate = percent < 0;
    if (percent >= 0)
      bar.Value = percent;
    var detail = activity + "  /  " + status + (percent < 0 ? "" : "  (" + percent + "%)");
    Find<TextBlock>("Location").Text = detail;
    Find<TextBlock>("Location").ToolTip = detail;
  }

  internal void Failure(string message) {
    var hasLog = Destination != null && File.Exists(LogPath);
    Show("Failure", "Needs attention", message, "Back to game", hasLog ? "Open launcher log" : null,
         null);
    if (!hasLog)
      return;
    try {
      Find<TextBox>("LogText").Text = File.ReadAllText(LogPath);
    } catch (IOException error) {
      Find<TextBox>("LogText").Text = "Could not read the log: " + error.Message;
    }
    Find<Expander>("ErrorLog").Visibility = Visibility.Visible;
    Find<Expander>("ErrorLog").IsExpanded = true;
  }

  internal void ShowRelease(Release? release) {
    latest = release;
    Find<Button>("ViewRelease").Content = release == null ? "All releases" : "View release";
    Find<TextBlock>("UpdateStatus").Text =
        release == null ? "No public releases yet."
        : Releases.IsNewer(release.Tag, Releases.GameVersion)
            ? "Recomp " + release.Tag + " is available."
            : "You're up to date with the latest stable release.";
    Find<TextBlock>("UpdateDetail").Text =
        release == null
            ? "You're using a development build. Published releases will appear here " +
                  "when available."
            : "Latest stable: " + release.Tag + ". Download its launcher, select your existing " +
                  ("game folder, then apply the build in Manage game. Saves and " +
                   "settings are kept.");
    Find<TextBlock>("NotesHeading").Text =
        release == null ? "What's new in this build" : "Release notes / " + release.Tag;
    var notes = release == null                           ? Releases.Resource("CHANGELOG.md")
                : String.IsNullOrWhiteSpace(release.Body) ? "No release notes provided."
                                                          : release.Body;
    Find<RichTextBox>("ReleaseNotes").Document =
        MarkdownNotes.Render(notes, uri => OpenPath(uri.AbsoluteUri));
  }

  internal async void CheckUpdates() {
    var button = Find<Button>("CheckUpdates");
    if (!button.IsEnabled)
      return;
    CheckingUpdates = true;
    button.IsEnabled = false;
    button.Content = "Checking...";
    Find<TextBlock>("UpdateStatus").Text = "Checking GitHub for a stable release...";
    try {
      var release = await Task.Run(delegate { return Releases.Latest(); });
      ShowRelease(release);
    } catch (Exception error) {
      Find<TextBlock>("UpdateStatus").Text = "Couldn't check for updates.";
      Find<TextBlock>("UpdateDetail").Text =
          "Check your connection and try again, or browse All releases. " + error.Message;
    } finally {
      CheckingUpdates = false;
      button.Content = "Check for updates";
      button.IsEnabled = !Busy;
    }
  }

  internal void CreateDesktopShortcut(string? desktopDirectory = null) {
    if (Busy || !Installer.Ready(Destination))
      return;
    try {
      using var installationLock =
          File.Open(Path.Combine(Destination!, "out/launcher/launcher.lock"), FileMode.OpenOrCreate,
                    FileAccess.ReadWrite, FileShare.None);
      if (!File.Exists(LauncherInstallation.PathFor(Destination!)))
        LauncherInstallation.Install(executable, Destination!);
      LauncherInstallation.CreateShortcut(
          Destination!, desktopDirectory ??
                            Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory));
      Find<TextBlock>("ShortcutStatus").Text = "Desktop shortcut created.";
    } catch (Exception error) {
      Find<TextBlock>("ShortcutStatus").Text =
          "Couldn't create the desktop shortcut. " + error.Message;
    }
  }

  internal async void Start(bool play, bool shaders, bool updateGame = false) {
    if (Busy)
      return;
    if (Destination is null) {
      Failure("Choose a game folder first.");
      return;
    }
    if (Settings.Dirty && !Settings.Save()) {
      SetPage("Settings");
      return;
    }
    var createDesktopShortcut = !play && (!updateGame || !Installer.Ready(Destination)) &&
                                Find<CheckBox>("DesktopShortcut").IsChecked == true;
    Busy = true;
    Show("Busy", play ? "Getting ready to play" : "Preparing your game",
         "Keep the launcher open while this finishes.", null, null, null);
    elapsed.Restart();
    stepLabel = "Starting";
    timer.Start();
    var progress = new Progress<InstallProgress>(delegate(InstallProgress update) {
      if (update.Step != 0) {
        elapsed.Restart();
        stepLabel = "Step " + update.Step + " of " + update.Total;
        Find<TextBlock>("Heading").Text = update.Activity;
        Find<TextBlock>("Description").Text = update.Status;
        if (update.Activity == "Game is running") {
          Find<Button>("Primary").Content = "Running";
          Find<TextBlock>("GameStatus").Text = "In game";
          Find<ProgressBar>("Progress").Visibility = Visibility.Collapsed;
        }
      }
      Progress(update.Activity, update.Status, update.Percent);
    });
    try {
      var installer = new Installer(kit, Destination, executable, progress);
      await Task.Run(delegate { installer.Run(Source, play, shaders, updateGame); });
      Busy = false;
      Settings.Reload();
      Home();
      if (createDesktopShortcut)
        CreateDesktopShortcut();
      if (shaders)
        Find<TextBlock>("Description").Text = "Shader preparation complete. Ready when you are.";
    } catch (Exception error) {
      Failure(error.Message);
    } finally {
      Busy = false;
      timer.Stop();
    }
  }
}
