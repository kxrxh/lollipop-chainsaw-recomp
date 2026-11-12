using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace lollipop {
internal static class Setup {
  internal static readonly Assembly Assembly = Assembly.GetExecutingAssembly();

  [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
  private static extern int SetCurrentProcessExplicitAppUserModelID(string appId);

  internal static void SetTaskbarIdentity() {
    Marshal.ThrowExceptionForHR(SetCurrentProcessExplicitAppUserModelID("LollipopChainsaw.Recomp.Setup"));
  }

  [STAThread]
  private static int Main() {
    try {
      SetTaskbarIdentity();
      System.Net.ServicePointManager.SecurityProtocol = System.Net.SecurityProtocolType.Tls12;
      var executable = Assembly.Location;
      bool created;
      using (var mutex = new Mutex(
                 true, "Local\\LollipopSetup-" + Tools.HashText(executable.ToUpperInvariant()),
                 out created)) {
        if (!created)
          throw new InvalidOperationException("This launcher is already open.");
        new Launcher(ExtractKit(), executable).Window.ShowDialog();
      }
      return 0;
    } catch (Exception error) {
      MessageBox.Show(error.Message, "Lollipop Chainsaw Setup", MessageBoxButton.OK,
                      MessageBoxImage.Error);
      return 1;
    }
  }

  internal static string ExtractKit() {
    using (var payload = Assembly.GetManifestResourceStream("kit.zip")) {
      if (payload == null)
        throw new InvalidDataException("The installer payload is missing.");
      var cache =
          Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                       "Lollipop Chainsaw Recomp", "installer");
      var kit = Path.Combine(cache, Tools.Hash(payload).Substring(0, 16));
      if (Directory.Exists(kit))
        return kit;
      Directory.CreateDirectory(cache);
      var stage = Path.Combine(cache, Guid.NewGuid().ToString("N"));
      Directory.CreateDirectory(stage);
      try {
        payload.Position = 0;
        using (var archive = new ZipArchive(payload, ZipArchiveMode.Read))
            Tools.Extract(archive, stage);
        try {
          Directory.Move(stage, kit);
        } catch (IOException) {
          if (!Directory.Exists(kit))
            throw;
        }
      } finally {
        Tools.DeleteStage(stage, cache);
      }
      return kit;
    }
  }
}

internal sealed class Launcher {
  internal readonly Window Window;
  internal string Source, Destination, Screen;
  internal bool Busy;
  private readonly string kit, executable;
  private readonly DispatcherTimer timer = new DispatcherTimer();
  private readonly Stopwatch elapsed = new Stopwatch();
  private string stepLabel = "";

  internal Launcher(string kit, string executable) {
    this.kit = kit;
    this.executable = executable;
    using (var xaml = Setup.Assembly.GetManifestResourceStream("launcher.xaml")) Window =
        (Window)XamlReader.Load(xaml);
    FitWorkArea(SystemParameters.WorkArea.Size);
    Window.Icon = Image("lollipop.ico");
    Find<ImageBrush>("Cover").ImageSource = Image("hero.png");
    Find<ImageBrush>("Paper").ImageSource = Image("paper.png");
    Find<Button>("ChooseIso").Click += delegate {
      var picker =
          new Microsoft.Win32.OpenFileDialog { Title = "Choose your Xbox 360 disc image",
                                               Filter = "Xbox 360 disc image (*.iso)|*.iso" };
      if (picker.ShowDialog(Window) == true) {
        Source = picker.FileName;
        Home();
      }
    };
    Find<Button>("ChooseFolder").Click += delegate {
      using (var picker =
                 new System.Windows.Forms.FolderBrowserDialog {
                   Description = "Choose or create a folder for Lollipop Chainsaw.",
                   ShowNewFolderButton = true
                 }) { if (picker.ShowDialog() ==
                          System.Windows.Forms.DialogResult.OK) { Destination = picker.SelectedPath;
    Home();
  }
}
};
Find<Button>("Primary").Click += delegate {
  if (Screen == "Failure")
    Home();
  else
    Start(Screen == "Ready", false);
};
Find<Button>("Precompile").Click += delegate { Start(true, true); };
Find<Button>("Secondary").Click += delegate {
  if (Screen == "Failure") {
    try {
      Process.Start("notepad.exe", Tools.Quote(LogPath));
    } catch (Exception error) {
      MessageBox.Show(Window, error.Message, "Could not open log");
    }
  } else {
    Destination = null;
    Home();
  }
};
Find<Button>("Help").Click += delegate {
  MessageBox.Show(
      Window,
      "Use the original Xbox 360 ISO, media ID 31BE13D9 (version 0.0.0.2). " +
          "Choose a folder with room for the game and its build files.\n\n" +
          "First-time setup needs internet. Allow the Windows approval prompt if it appears. " +
          "After a restart, choose the same ISO and folder to continue.\n\n" +
          "When installed, open Play Lollipop Chainsaw.exe in that folder. Your saves are in its " +
          "user folder.",
      "Setup help");
};
Window.Closing += delegate(object sender, System.ComponentModel.CancelEventArgs args) {
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
  Find<TextBlock>("StepStatus").Text = stepLabel + "  /  " + elapsed.Elapsed.ToString(@"mm\:ss");
};
var ownFolder = Path.GetDirectoryName(executable);
if (Installer.Ready(ownFolder))
  Destination = ownFolder;
Home();
}

internal T Find<T>(string name)
    where T : class { return (T)Window.FindName(name); }
internal void FitWorkArea(Size area) {
  Window.MinWidth = Math.Min(Window.MinWidth, area.Width);
  Window.MinHeight = Math.Min(Window.MinHeight, area.Height);
  Window.Width = Math.Min(Window.Width, area.Width);
  Window.Height = Math.Min(Window.Height, area.Height);
}
private string LogPath {
  get { return Path.Combine(Destination, "out/launcher/latest.log"); }
}

private static BitmapSource Image(string name) {
  using (var stream = Setup.Assembly.GetManifestResourceStream(name)) {
    var image = BitmapFrame.Create(stream, BitmapCreateOptions.None, BitmapCacheOption.OnLoad);
    image.Freeze();
    return image;
  }
}

internal void Show(string state, string tab, string title, string body, string action,
                   string alternative, string detail) {
  Screen = state;
  Find<TextBlock>("Heading").Text = title;
  Find<TextBlock>("Description").Text = body;
  Find<TextBlock>("Location").Text = detail ?? "";
  Find<StackPanel>("Inputs").Visibility =
      state == "Install" ? Visibility.Visible : Visibility.Collapsed;
  var precompile = Find<Button>("Precompile");
  precompile.Visibility = state == "Ready" ? Visibility.Visible : Visibility.Collapsed;
  precompile.IsEnabled =
      Destination != null &&
      File.Exists(
          Path.Combine(Destination, "out/build/win-amd64-release/shader-precompile-v1.ready"));
  precompile.ToolTip =
      precompile.IsEnabled
          ? "Prepare the available pipeline inventory. Coverage depends on the recorded scenes."
          : "Update or rebuild this installation to enable shader preparation.";
  foreach (var name in new[] { "DiscTab", "SetupTab", "PlayTab" }) {
    var label = Find<TextBlock>(name);
    label.Foreground =
        (Brush) new BrushConverter().ConvertFromString(name == tab ? "#B91C5B" : "#796471");
    label.TextDecorations = name == tab ? TextDecorations.Underline : null;
  }
  var primary = Find<Button>("Primary");
  primary.Content = action;
  primary.Visibility = action == null ? Visibility.Collapsed : Visibility.Visible;
  primary.IsEnabled = true;
  Find<Button>("Secondary").Content = alternative;
  Find<Button>("Secondary").Visibility =
      alternative == null ? Visibility.Collapsed : Visibility.Visible;
  Find<ProgressBar>("Progress").Visibility =
      state == "Busy" ? Visibility.Visible : Visibility.Collapsed;
  Find<ProgressBar>("Progress").IsIndeterminate = true;
  Find<TextBlock>("StepStatus").Visibility = Find<ProgressBar>("Progress").Visibility;
  Find<TextBlock>("StepStatus").Text = "";
  Find<Expander>("ErrorLog").Visibility = Visibility.Collapsed;
  Find<TextBox>("LogText").Clear();
  if (action != null && Window.IsVisible)
    primary.Focus();
}

internal void Home() {
  if (Installer.Ready(Destination)) {
    Show("Ready", "PlayTab", "Ready to play",
         "Your game is installed. Next time, open " +
             "Play Lollipop Chainsaw.exe in your install folder.",
         "Play Lollipop Chainsaw", "Choose another folder", Destination);
    return;
  }
  Show("Install", "DiscTab", "Install your game",
       "Choose your Xbox 360 ISO and an install folder. " +
           "First-time setup needs an internet connection.",
       "Install game", null, null);
  Find<TextBlock>("IsoName").Text =
      Source == null ? "Choose an ISO file" : Path.GetFileName(Source);
  Find<TextBlock>("FolderName").Text = Destination ?? "Choose a folder";
  Find<Button>("ChooseIso").ToolTip = Source;
  Find<Button>("ChooseFolder").ToolTip = Destination;
  Find<Button>("Primary").IsEnabled = Source != null && Destination != null;
}

internal void Progress(string activity, string status, int percent) {
  var bar = Find<ProgressBar>("Progress");
  bar.IsIndeterminate = percent < 0;
  if (percent >= 0)
    bar.Value = percent;
  Find<TextBlock>("Location").Text =
      activity + "  /  " + status + (percent < 0 ? "" : "  (" + percent + "%)");
}

internal void Failure(string message) {
  var hasLog = Destination != null && File.Exists(LogPath);
  Show("Failure", "SetupTab", "Needs attention", message, "Back", hasLog ? "Open setup log" : null,
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

internal async void Start(bool play, bool shaders) {
  if (Busy)
    return;
  Busy = true;
  Show("Busy", play ? "PlayTab" : "SetupTab", "Getting ready",
       "Keep the launcher open while setup runs.", null, null, null);
  elapsed.Restart();
  stepLabel = "";
  timer.Start();
  var progress = new Progress<InstallProgress>(delegate(InstallProgress update) {
    if (update.Step != 0) {
      elapsed.Restart();
      stepLabel = "Step " + update.Step + " / " + update.Total;
      Find<TextBlock>("Heading").Text = update.Activity;
      Find<TextBlock>("Description").Text = update.Status;
    }
    Progress(update.Activity, update.Status, update.Percent);
  });
  try {
    var installer = new Installer(kit, Destination, executable, progress);
    await Task.Run(delegate { installer.Run(Source, play, shaders); });
    Home();
  } catch (Exception error) {
    Failure(error.Message);
  } finally {
    Busy = false;
    timer.Stop();
  }
}
}
}
