namespace lollipop;

internal sealed class InstallProgress {
  internal string Activity = "", Status = "";
  internal int Percent = -1, Step, Total;
}

internal sealed class Package {
  internal readonly string Name, Url, Hash, File;
  internal Package(string name, string url, string hash, string file) {
    Name = name;
    Url = url;
    Hash = hash;
    File = file;
  }
}
