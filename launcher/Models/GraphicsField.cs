namespace lollipop;

internal sealed class GraphicsField {
  internal readonly string Name, Label, Help, Default;
  internal readonly string[] Choices;
  internal GraphicsField(string name, string label, string help, string initial,
                         params string[] choices) {
    Name = name;
    Label = label;
    Help = help;
    Default = initial;
    Choices = choices;
  }
}
