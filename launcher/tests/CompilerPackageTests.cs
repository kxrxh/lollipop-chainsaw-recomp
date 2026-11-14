using System;
using System.IO;
using System.Linq;

namespace lollipop;

internal static class CompilerPackageTests {
  internal static void Run(string root, string sandbox) {
    var llvm = Path.Combine(sandbox, "LLVM fixture [1] Ж");
    var bin = Path.Combine(llvm, "bin");
    Directory.CreateDirectory(bin);
    var fixture = Path.Combine(sandbox, "compiler-fixture.ps1");
    File.WriteAllText(fixture, """
        param([string]$OutputFile)
        $ErrorActionPreference = 'Stop'
        Add-Type -OutputAssembly $OutputFile -OutputType ConsoleApplication -TypeDefinition @'
        public static class CompilerFixture {
          public static void Main() {
            System.Console.WriteLine("clang version 20.1.8\nTarget: x86_64-pc-windows-msvc");
          }
        }
        '@
        """);
    var tools = new Tools(new RecordedProgress(), _ => { });
    var powershell =
        Path.Combine(Environment.SystemDirectory, "WindowsPowerShell/v1.0/powershell.exe");
    var fixtureExe = Path.Combine(sandbox, "compiler-fixture.exe");
    tools.Run(powershell, sandbox,
              ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", fixture, "-OutputFile",
               fixtureExe], true);
    File.Copy(fixtureExe, Path.Combine(bin, "clang++.exe"));
    foreach (var name in new[] { "clang.exe", "lld-link.exe", "llvm-ar.exe", "llvm-lib.exe",
                                  "llvm-ranlib.exe", "llvm-objdump.exe", "unused.pdb" })
      File.Copy(Path.Combine(bin, "clang++.exe"), Path.Combine(bin, name));
    var headers = Path.Combine(llvm, "lib/clang/20/include");
    Directory.CreateDirectory(headers);
    File.WriteAllText(Path.Combine(headers, "stddef.h"), "// builtin header fixture");
    var output = Path.Combine(sandbox, "compiler output", "llvm.tar.xz");
    var arguments = new[] { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                            Path.Combine(root, "launcher/scripts/Pack-Compiler.ps1"),
                            "-LlvmDirectory", bin, "-OutputFile", output };
    tools.Run(powershell, sandbox, arguments, true);
    var hash = Tools.HashFile(output);
    var inputTime = DateTime.UtcNow.AddDays(2);
    foreach (var file in Directory.GetFiles(llvm, "*", SearchOption.AllDirectories))
      File.SetLastWriteTimeUtc(file, inputTime);
    tools.Run(powershell, sandbox, arguments, true);
    if (Tools.HashFile(output) != hash ||
        Directory.GetFiles(llvm, "*", SearchOption.AllDirectories)
            .Any(file => File.GetLastWriteTimeUtc(file) != inputTime))
      throw new Exception("Repackaging unchanged LLVM changed its cache identity or source timestamps");
    using (var payload = File.OpenRead(output)) {
      var compiler = tools.Get(new Package("LLVM", "llvm.tar.xz", hash, "bin/clang++.exe"),
                               Path.Combine(sandbox, "embedded compiler"), false, payload);
      var unpacked = Path.GetDirectoryName(Path.GetDirectoryName(compiler))!;
      var files = Directory.GetFiles(unpacked, "*", SearchOption.AllDirectories);
      if (Directory.GetFiles(Path.GetDirectoryName(compiler)!).Length != 6 ||
          files.Any(file => file.EndsWith(".pdb") || file.EndsWith("llvm-objdump.exe")) ||
          !File.Exists(Path.Combine(unpacked, "lib/clang/20/include/stddef.h")) ||
          !File.Exists(Path.Combine(unpacked, "licenses/LICENSE.LLVM")) ||
          !tools.Capture(compiler, sandbox, "--version").Contains("clang version 20.1.8"))
        throw new Exception("The embedded compiler lost required files or included unused tools");
    }
    File.Delete(Path.Combine(bin, "lld-link.exe"));
    if (tools.Run(powershell, sandbox, arguments, false) == 0 || Tools.HashFile(output) != hash)
      throw new Exception("Invalid compiler input overwrote the previous package");
    if (!File.Exists(Path.Combine(bin, "unused.pdb")))
      throw new Exception("Compiler packaging changed its source installation");
    using var badPayload = File.OpenRead(output);
    try {
      tools.Get(new Package("LLVM", "llvm.tar.xz", new string('0', 64), "bin/clang++.exe"),
                Path.Combine(sandbox, "invalid embedded compiler"), false, badPayload);
    } catch (InvalidDataException) {
      return;
    }
    throw new Exception("An embedded compiler with the wrong digest was accepted");
  }
}
