using System;
using System.IO;
using System.Text;

namespace lollipop;

internal static class GameLocation {
  private const int MaximumSize = 128 * 1024;
  internal static readonly string LegacyFile =
      Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                   "Lollipop Chainsaw Recomp", "launcher", "game-location.txt");
  internal static readonly string DataDirectory =
      Path.Combine(AppContext.BaseDirectory, "launcher-data");
  internal static readonly string DefaultFile = Path.Combine(DataDirectory, "game-location.txt");

  internal static string? Load(string file, string? legacyFile = null) {
    if (!File.Exists(file) && legacyFile != null)
      return Load(legacyFile);
    try {
      if (new FileInfo(file).Length > MaximumSize)
        return null;
      var folder = File.ReadAllText(file, new UTF8Encoding(false, true));
      if (!Path.IsPathFullyQualified(folder) || folder.IndexOfAny(['\0', '\r', '\n']) >= 0)
        return null;
      folder = Path.GetFullPath(folder);
      return Directory.Exists(folder) ? folder : null;
    } catch (Exception error) when (error is IOException or UnauthorizedAccessException or
                                        ArgumentException or NotSupportedException) {
      return null;
    }
  }

  internal static bool Save(string file, string folder) {
    try {
      folder = Path.GetFullPath(folder);
      if (!Directory.Exists(folder))
        return false;
      if (File.Exists(file) && new FileInfo(file).Length <= MaximumSize &&
          File.ReadAllText(file) == folder)
        return true;
      file = Path.GetFullPath(file);
      Directory.CreateDirectory(Path.GetDirectoryName(file)!);
      var temporary = file + "." + Guid.NewGuid().ToString("N") + ".tmp";
      try {
        File.WriteAllText(temporary, folder, new UTF8Encoding(false));
        if (File.Exists(file))
          File.Replace(temporary, file, null);
        else
          File.Move(temporary, file);
      } finally {
        File.Delete(temporary);
      }
      return true;
    } catch (Exception error) when (error is IOException or UnauthorizedAccessException or
                                        ArgumentException or NotSupportedException) {
      return false;
    }
  }
}
