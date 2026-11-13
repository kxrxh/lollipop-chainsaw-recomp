using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Media;

namespace lollipop;

internal sealed class GraphicsSettings {
  private readonly Launcher launcher;
  internal readonly Dictionary<string, ComboBox> Controls = new Dictionary<string, ComboBox>();
  private GameSettings? profile;
  private string? folder;
  private bool loading;
  internal bool Dirty;

  internal GraphicsSettings(Launcher launcher) {
    this.launcher = launcher;
    var form = launcher.Find<StackPanel>("SettingsFields");
    foreach (var field in GameSettings.Fields) {
      if (field.Name == "window_size" || field.Name == "resolution_scale")
        form.Children.Add(
            new TextBlock { Text = field.Name == "window_size" ? "Display" : "Graphics",
                            FontSize = 18, FontWeight = FontWeights.SemiBold,
                            Margin = new Thickness(0, 8, 0, 20) });
      var row = new Grid { Margin = new Thickness(0, 0, 0, 18) };
      row.ColumnDefinitions.Add(new ColumnDefinition());
      row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(260) });
      var label = new StackPanel { Margin = new Thickness(0, 0, 20, 0),
                                   VerticalAlignment = VerticalAlignment.Center };
      label.Children.Add(new TextBlock { Text = field.Label, FontWeight = FontWeights.SemiBold });
      label.Children.Add(new TextBlock { Text = field.Help,
                                         Foreground = (Brush)launcher.Window.FindResource("Muted"),
                                         FontSize = 12, TextWrapping = TextWrapping.Wrap,
                                         Margin = new Thickness(0, 5, 0, 0) });
      var control =
          new ComboBox { VerticalAlignment = VerticalAlignment.Center, ToolTip = field.Help };
      AutomationProperties.SetName(control, field.Label);
      control.SelectionChanged += delegate {
        if (!loading) {
          Dirty = true;
          Status("Unsaved changes. Save settings or press Play to apply them.");
          Refresh(launcher.Destination, launcher.Busy);
        }
      };
      Grid.SetColumn(control, 1);
      row.Children.Add(label);
      row.Children.Add(control);
      form.Children.Add(row);
      Controls.Add(field.Name, control);
    }
    launcher.Find<Button>("SaveSettings").Click += delegate { Save(); };
    launcher.Find<Button>("ReloadSettings").Click += delegate { Reload(); };
    launcher.Find<Button>("RecommendedSettings").Click += delegate {
      Select(new Dictionary<string, string>(GameSettings.Fields.ToDictionary(
          field => field.Name,
          field => field.Name == "swap_post_effect" ? "fxaa" : field.Default)));
      Dirty = true;
      Status("Recommended settings selected. Save settings or press Play to apply them.");
      Refresh(launcher.Destination, launcher.Busy);
    };
    Reload();
  }

  private void Status(string message) { launcher.Find<TextBlock>("SettingsStatus").Text = message; }

  private void Select(IDictionary<string, string> values) {
    loading = true;
    foreach (var field in GameSettings.Fields) {
      var control = Controls[field.Name];
      control.Items.Clear();
      foreach (var choice in field.Choices) {
        var parts = choice.Split('|');
        control.Items.Add(new ComboBoxItem { Tag = parts[0], Content = parts[1] });
      }
      var selected = control.Items.Cast<ComboBoxItem>().FirstOrDefault(item => (string)item.Tag ==
                                                                               values[field.Name]);
      if (selected == null) {
        selected = new ComboBoxItem { Tag = values[field.Name],
                                      Content = values[field.Name] + " / Custom" };
        control.Items.Add(selected);
      }
      control.SelectedItem = selected;
    }
    loading = false;
  }

  internal void Reload() {
    folder = launcher.Destination;
    profile = null;
    loading = true;
    foreach (var control in Controls.Values)
      control.Items.Clear();
    loading = false;
    Dirty = false;
    try {
      if (Installer.Ready(folder) || GameUpdate.CanUpdate(folder)) {
        profile = GameSettings.Load(folder!);
        Select(profile.Values);
        Status("Changes apply on the next launch. Your audio and control settings are kept.");
      } else {
        Status("Locate an installed game to configure graphics and display.");
      }
    } catch (Exception error) {
      Status(error.Message);
    }
    Refresh(folder, launcher.Busy);
  }

  internal void Refresh(string? destination, bool busy) {
    if (folder != destination) {
      Reload();
      return;
    }
    launcher.Find<StackPanel>("SettingsFields").IsEnabled = profile != null && !busy;
    launcher.Find<Button>("SaveSettings").IsEnabled = profile != null && Dirty && !busy;
    launcher.Find<Button>("ReloadSettings").IsEnabled = destination != null && !busy;
    launcher.Find<Button>("RecommendedSettings").IsEnabled = profile != null && !busy;
  }

  internal bool Save() {
    if (launcher.Busy || profile == null)
      return false;
    try {
      var selected = Controls.ToDictionary(
          pair => pair.Key, pair => (string)((ComboBoxItem)pair.Value.SelectedItem).Tag);
      profile.Save(folder!, Environment.ProcessPath!, selected);
      profile = GameSettings.Load(folder!);
      Dirty = false;
      Status("Saved. These settings apply on the next launch.");
      Refresh(folder, false);
      return true;
    } catch (Exception error) {
      Status("Could not save settings. " + error.Message);
      return false;
    }
  }
}
