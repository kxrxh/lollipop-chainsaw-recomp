using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace lollipop;

internal sealed class CostumeImport : IDisposable {
  internal readonly string Directory;
  internal readonly List<CostumePack> Packs = new();
  internal CostumeImport(string directory) { Directory = directory; }
  public void Dispose() { Tools.DeleteStage(Directory, Path.GetDirectoryName(Directory)!); }
}

internal static class CostumeDlc {
  internal const string TitlePath = "user/0000000000000000/565707D0";
  private const int HeaderSize = 0x971a;
  private const long MaximumPackage = 64 * 1024 * 1024;
  private const long MaximumImport = 256 * 1024 * 1024;

  internal static CostumePack Inspect(Stream stream, string source, string file) {
    var bytes = new byte[HeaderSize];
    stream.ReadExactly(bytes);
    var magic = Encoding.ASCII.GetString(bytes, 0, 4);
    uint Read(int offset) => BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(offset, 4));
    if (magic is not("CON " or "LIVE" or "PIRS") || Read(0x360) != 0x565707d0 ||
        Read(0x344) != 2 || Read(0x3a9) != 0 || Read(0x340) < HeaderSize ||
        Read(0x340) > stream.Length || stream.Length > MaximumPackage)
      throw new InvalidDataException(
          "Choose Xbox 360 costume DLC for Lollipop Chainsaw (565707D0). " + source);
    var id = Convert.ToHexString(bytes.AsSpan(0x32c, 20));
    if (id.All(c => c == '0'))
      throw new InvalidDataException("The DLC package has no content ID. " + source);
    var name = Encoding.BigEndianUnicode.GetString(bytes, 0x411, 0x100).Split('\0') [0].Trim();
    return new CostumePack(id, name.Length == 0 ? "Costume DLC" : name, source, file,
                           stream.Length);
  }

  internal static CostumeImport Scan(IEnumerable<string> sources, string stagingRoot) {
    var import = new CostumeImport(Path.Combine(stagingRoot, Guid.NewGuid().ToString("N")));
    System.IO.Directory.CreateDirectory(import.Directory);
    long total = 0;
    var count = 0;
    try {
      void ReadPackage(Stream stream, long size, string source) {
        if (++count > 4096 || size > MaximumPackage || (total += size) > MaximumImport)
          throw new InvalidDataException(
              "This selection is too large. Choose only your costume DLC packages.");
        Span<byte> magic = stackalloc byte[4];
        if (stream.ReadAtLeast(magic, 4, false) != 4 ||
            !(magic.SequenceEqual("CON "u8) || magic.SequenceEqual("LIVE"u8) ||
              magic.SequenceEqual("PIRS"u8)))
          return;
        // ZIP entries are copied to our own names; archive paths never become filesystem paths.
        var path = Path.Combine(import.Directory, Guid.NewGuid().ToString("N"));
        using (var output = File.Create(path)) {
          output.Write(magic);
          var buffer = new byte[81920];
          long copied = 4;
          int read;
          while ((read = stream.Read(buffer)) != 0) {
            if ((copied += read) > MaximumPackage || copied > size)
              throw new InvalidDataException("The DLC package exceeds its declared size.");
            output.Write(buffer, 0, read);
          }
          if (copied != size)
            throw new InvalidDataException("The DLC package is truncated.");
        }
        using var input = File.OpenRead(path);
        var pack = Inspect(input, source, path);
        var duplicate = import.Packs.Find(p => p.Id == pack.Id);
        if (duplicate == null)
          import.Packs.Add(pack);
        else if (Tools.HashFile(duplicate.File) != Tools.HashFile(pack.File))
          throw new InvalidDataException("Two different packages have the same content ID.");
      }
      void ReadFile(string path) {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
          return;
        if (Path.GetExtension(path).Equals(".zip", StringComparison.OrdinalIgnoreCase)) {
          using var zip = ZipFile.OpenRead(path);
          if (zip.Entries.Count > 4096)
            throw new InvalidDataException("The ZIP contains too many files.");
          foreach (var entry in zip.Entries.Where(e => e.Name.Length > 0)) {
            using var stream = entry.Open();
            ReadPackage(stream, entry.Length, Path.GetFileName(path));
          }
        } else {
          using var stream = File.OpenRead(path);
          ReadPackage(stream, stream.Length, Path.GetFileName(path));
        }
      }
      foreach (var source in sources) {
        if (System.IO.Directory.Exists(source)) {
          if ((File.GetAttributes(source) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("Choose a regular folder, rather than a folder link.");
          var options = new EnumerationOptions { RecurseSubdirectories = true,
                                                 AttributesToSkip = FileAttributes.ReparsePoint,
                                                 MaxRecursionDepth = 16 };
          foreach (var file in System.IO.Directory.EnumerateFiles(source, "*", options))
            ReadFile(file);
        } else
          ReadFile(source);
      }
      if (import.Packs.Count == 0)
        throw new InvalidDataException("No Xbox 360 costume DLC packages found. Choose a DLC ZIP " +
                                       "or a folder containing the original packages.");
      return import;
    } catch {
      import.Dispose();
      throw;
    }
  }

  internal static string PackagePath(string destination, string id) => Path.Combine(destination,
                                                                                    TitlePath,
                                                                                    "00000002", id);
  private static string HeaderPath(string destination,
                                   string id) => Path.Combine(destination, TitlePath,
                                                              "Headers/00000002", id + ".header");

  internal static bool Installed(string destination, string id) =>
      System.IO.Directory.Exists(PackagePath(destination, id)) && File.Exists(
                                                                      HeaderPath(destination, id));

  internal static List<CostumePack> List(string? destination) {
    var packs = new List<CostumePack>();
    if (destination == null)
      return packs;
    var root = Path.Combine(destination, TitlePath, "Headers/00000002");
    if (!System.IO.Directory.Exists(root))
      return packs;
    foreach (var path in System.IO.Directory.EnumerateFiles(root, "*.header")) {
      var id = Path.GetFileNameWithoutExtension(path);
      if (!Regex.IsMatch(id, "\\A[0-9A-Fa-f]{40,42}\\z") ||
          !System.IO.Directory.Exists(PackagePath(destination, id)))
        continue;
      using var file = File.OpenRead(path);
      if (file.Length < 0x148)
        continue;
      var header = new byte[0x148];
      file.ReadExactly(header);
      if (BinaryPrimitives.ReadUInt32BigEndian(header.AsSpan(4)) != 2 ||
          BinaryPrimitives.ReadUInt32BigEndian(header.AsSpan(0x140)) != 0x565707d0)
        continue;
      var name = Encoding.BigEndianUnicode.GetString(header, 8, 256).Split('\0')[0];
      packs.Add(new CostumePack(id, name.Length == 0 ? "Costume DLC" : name, "Installed", path, 0));
    }
    return packs.OrderBy(p => p.Name).ToList();
  }

  internal static void Install(CostumeImport import, string destination, string launcher,
                               Tools tools) {
    var state = Path.Combine(destination, "out/launcher");
    System.IO.Directory.CreateDirectory(state);
    using var installationLock =
        File.Open(Path.Combine(state, "launcher.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite,
                  FileShare.None);
    GameUpdate.CheckClosed(destination, launcher);
    var helper = Path.Combine(destination, "out/build/win-amd64-release/lollipop_dlc_tool.exe");
    if (!File.Exists(helper))
      throw new IOException(
          "Apply this launcher build from Manage game before installing costumes.");
    foreach (var pack in import.Packs) {
      var target = PackagePath(destination, pack.Id);
      var header = HeaderPath(destination, pack.Id);
      foreach (var path in new[] { target, header })
        for (var parent = new DirectoryInfo(Path.GetDirectoryName(path)!); parent != null;
             parent = parent.Parent)
          if (parent.Exists && (parent.Attributes & FileAttributes.ReparsePoint) != 0)
            throw new IOException("The DLC destination contains a folder link.");
      if (Installed(destination, pack.Id))
        continue;
      if (System.IO.Directory.Exists(target) || File.Exists(header))
        throw new IOException(
            "An incomplete or existing package was kept. Check the game folder before retrying: " +
            pack.Name);
    }
    // Fully validate every new package before publishing any of them.
    var pending = import.Packs.Where(p => !Installed(destination, p.Id)).ToList();
    var attempt = Path.Combine(import.Directory, Guid.NewGuid().ToString("N"));
    System.IO.Directory.CreateDirectory(attempt);
    foreach (var pack in pending) {
      tools.Report("Installing costumes", "Reading " + pack.Name, -1);
      tools.Run(helper, destination, new[] { "prepare", pack.File, Path.Combine(attempt, pack.Id) },
                true);
    }
    foreach (var pack in pending) {
      var prepared = Path.Combine(attempt, pack.Id);
      var target = PackagePath(destination, pack.Id);
      var header = HeaderPath(destination, pack.Id);
      System.IO.Directory.CreateDirectory(Path.GetDirectoryName(target)!);
      System.IO.Directory.CreateDirectory(Path.GetDirectoryName(header)!);
      File.Move(Path.Combine(prepared, "content.header"), header);
      try {
        System.IO.Directory.Move(Path.Combine(prepared, "content"), target);
      } catch {
        File.Delete(header);
        throw;
      }
    }
  }
}
