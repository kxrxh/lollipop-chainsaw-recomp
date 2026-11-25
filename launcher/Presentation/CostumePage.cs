using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace lollipop;

internal sealed class CostumePage {
  private readonly Launcher launcher;
  private readonly string executable;
  private CostumeImport? selection;
  private bool working;
  private string? selectedDestination;

  internal CostumePage(Launcher launcher, string executable) {
    this.launcher = launcher;
    this.executable = executable;
    launcher.Find<Button>("ChooseDlcZips").Click += async delegate {
      var picker = new Microsoft.Win32.OpenFileDialog { Title = "Choose your costume DLC ZIPs",
                                                        Filter = "Costume DLC ZIPs (*.zip)|*.zip",
                                                        Multiselect = true };
      if (picker.ShowDialog(launcher.Window) == true)
        await Select(picker.FileNames);
    };
    launcher.Find<Button>("ChooseDlcFolder").Click += async delegate {
      var picker = new Microsoft.Win32.OpenFolderDialog {
        Title = "Choose a folder containing your costume DLC"
      };
      if (picker.ShowDialog(launcher.Window) == true)
        await Select([picker.FolderName]);
    };
    launcher.Find<Button>("ClearCostumes").Click += delegate {
      Clear();
      Refresh();
    };
    launcher.Find<Button>("InstallCostumes").Click += async delegate { await Install(); };
    launcher.Find<Button>("RefreshCostumes").Click += delegate { Refresh(); };
    launcher.Window.Closed += delegate { Clear(); };
  }

  private static Border Row(CostumePack pack, string status) {
    var grid = new Grid();
    grid.ColumnDefinitions.Add(new ColumnDefinition());
    grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
    var text = new StackPanel { Margin = new Thickness(0, 0, 18, 0) };
    text.Children.Add(new TextBlock { Text = pack.Name, FontWeight = FontWeights.SemiBold,
                                      TextWrapping = TextWrapping.Wrap });
    text.Children.Add(
        new TextBlock { Text = pack.Source,
                        Foreground = new SolidColorBrush(Color.FromRgb(193, 183, 199)),
                        FontSize = 11, TextTrimming = TextTrimming.CharacterEllipsis,
                        ToolTip = pack.Source, Margin = new Thickness(0, 5, 0, 0) });
    grid.Children.Add(text);
    var badge = new TextBlock { Text = status, FontSize = 12,
                                Foreground = new SolidColorBrush(
                                    status == "Installed" ? Color.FromRgb(127, 227, 176)
                                                          : Color.FromRgb(255, 145, 195)),
                                VerticalAlignment = VerticalAlignment.Center };
    Grid.SetColumn(badge, 1);
    grid.Children.Add(badge);
    return new Border { Background = new SolidColorBrush(Color.FromArgb(96, 27, 26, 34)),
                        CornerRadius = new CornerRadius(14), Padding = new Thickness(16),
                        Margin = new Thickness(0, 0, 0, 8), Child = grid };
  }

  private void Status(string message) { launcher.Find<TextBlock>("CostumeStatus").Text = message; }

  internal void Refresh() {
    if (selectedDestination != launcher.Destination && !working) {
      Clear();
      selectedDestination = launcher.Destination;
    }
    var ready = Installer.Ready(launcher.Destination);
    var available = ready && !launcher.Busy && !working;
    foreach (var name in new[] { "ChooseDlcZips", "ChooseDlcFolder", "ClearCostumes",
                                 "RefreshCostumes" })
      launcher.Find<Button>(name).IsEnabled = available;
    var candidates = launcher.Find<ItemsControl>("CostumeCandidates");
    candidates.Items.Clear();
    if (selection != null)
      foreach (var pack in selection.Packs)
        candidates.Items.Add(Row(
            pack, CostumeDlc.Installed(launcher.Destination!, pack.Id) ? "Installed" : "Ready"));
    launcher.Find<StackPanel>("CostumeReview").Visibility =
        selection == null ? Visibility.Collapsed : Visibility.Visible;
    launcher.Find<WrapPanel>("CostumeInstallActions").Visibility =
        selection == null ? Visibility.Collapsed : Visibility.Visible;
    var count =
        selection?.Packs.Count(p => !CostumeDlc.Installed(launcher.Destination!, p.Id)) ?? 0;
    launcher.Find<Button>("InstallCostumes").Content =
        count == 1 ? "Install 1 pack" : "Install " + count + " packs";
    launcher.Find<Button>("InstallCostumes").IsEnabled = available && count > 0;
    var installed = launcher.Find<ItemsControl>("InstalledCostumes");
    installed.Items.Clear();
    try {
      foreach (var pack in CostumeDlc.List(launcher.Destination))
        installed.Items.Add(Row(pack, "Installed"));
    } catch (Exception error) when (error is IOException or UnauthorizedAccessException) {
      Status("Couldn't read installed costumes. " + error.Message);
    }
    launcher.Find<TextBlock>("NoCostumes").Visibility =
        installed.Items.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
    if (!ready)
      Status("Install or locate your game first, then add your costume DLC here.");
    else if (!working && selection == null)
      Status("Use your own Lollipop Chainsaw Xbox 360 DLC. ZIPs and nested package folders are " +
             "supported.");
  }

  private void Clear() {
    selection?.Dispose();
    selection = null;
  }

  internal async Task Select(string[] sources) {
    if (launcher.Busy || working || !Installer.Ready(launcher.Destination))
      return;
    Clear();
    working = true;
    launcher.Busy = true;
    launcher.Show("Busy", "Reading costume packs",
                  "Checking your selection. Keep the launcher open.", null, null, null);
    Status("Reading your costume packages…");
    try {
      selection = await Task.Run(
          () => CostumeDlc.Scan(sources, Path.Combine(launcher.Destination!, "out/launcher/dlc")));
      selectedDestination = launcher.Destination;
      Status("Found " + selection.Packs.Count +
             " costume pack(s). Review the selection, then install.");
    } catch (Exception error) {
      Status("Couldn't read costume packs. " + error.Message);
    } finally {
      working = false;
      launcher.Busy = false;
      var status = launcher.Find<TextBlock>("CostumeStatus").Text;
      launcher.Home();
      Status(status);
    }
  }

  internal async Task Install() {
    if (launcher.Busy || working || selection == null || !Installer.Ready(launcher.Destination))
      return;
    working = true;
    launcher.Busy = true;
    launcher.Show("Busy", "Installing costumes",
                  "Keep the launcher open until installation finishes.", null, null, null);
    var progress = new Progress<InstallProgress>(p => Status(p.Status));
    var tools = new Tools(progress,
                          _ => {});
    var status =
        "Costume packs installed. Restart the game to use your costumes.";
    try {
      await Task.Run(() => CostumeDlc.Install(selection, launcher.Destination!, executable, tools));
      Clear();
    } catch (Exception error) {
      status = "Couldn't install costumes. " + error.Message;
    } finally {
      working = false;
      launcher.Busy = false;
      launcher.Home();
      Status(status);
    }
  }
}
