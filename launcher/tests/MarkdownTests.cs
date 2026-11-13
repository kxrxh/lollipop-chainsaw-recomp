using System;
using System.Linq;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Navigation;

namespace lollipop;

internal static class MarkdownTests {
  private static void Check(bool condition, string message) {
    if (!condition)
      throw new Exception(message);
  }

  internal static void Run() {
    var document = MarkdownNotes.Render(
        "# Changelog\n\n## Unreleased\n\n- **New** launcher\n- `Settings`\n\n" +
            "3. First\n4. Second\n\n> A quote\n\n```text\nexample()\n```\n\n" +
            "| Version | Status |\n| --- | --- |\n| 0.2.0 | Ready |",
        _ => {});
    var blocks = document.Blocks.Cast<Block>().ToArray();
    Check(blocks.Length == 7 && blocks[0] is Paragraph { FontSize : 25 } &&
              blocks[1] is Paragraph { FontSize : 20 },
          "Markdown headings were not rendered");
    var bullets = (System.Windows.Documents.List)blocks[2];
    Check(bullets.ListItems.Count == 2 && bullets.MarkerStyle == TextMarkerStyle.Disc &&
              ((Paragraph)bullets.ListItems.FirstListItem.Blocks.FirstBlock)
                  .Inlines.FirstInline is Span { FontWeight : var weight } &&
              weight == FontWeights.Bold,
          "Markdown bullets or bold text were lost");
    Check(blocks[3] is System.Windows.Documents.List { StartIndex : 3, ListItems.Count : 2 } &&
              blocks[4] is Section && blocks[5] is Paragraph { FontFamily.Source : "Consolas" } &&
              blocks[6] is Table { RowGroups.Count : 1 } table &&
              table.RowGroups[0].Rows.Count == 2,
          "Markdown numbering, quotes, code or tables were lost");

    Uri? opened = null;
    var links = MarkdownNotes.Render(
        "[Read more](docs/BUILDING.md) [unsafe](javascript:alert) " +
            "![Cover](https://example.com/image.png) <script>alert(1)</script>",
        uri => opened = uri);
    var paragraph = (Paragraph)links.Blocks.FirstBlock;
    var hyperlink = paragraph.Inlines.OfType<Hyperlink>().Single();
    Check(hyperlink.NavigateUri.AbsoluteUri ==
                  "https://github.com/kxrxh/lollipop-chainsaw-recomp/blob/main/docs/BUILDING.md" &&
              new TextRange(links.ContentStart, links.ContentEnd).Text.Contains("<script>"),
          "Relative links or inert HTML rendering changed");
    hyperlink.RaiseEvent(new RequestNavigateEventArgs(
        hyperlink.NavigateUri, "") { RoutedEvent = Hyperlink.RequestNavigateEvent });
    Check(opened == hyperlink.NavigateUri && !paragraph.Inlines.OfType<InlineUIContainer>().Any(),
          "Markdown link activation failed or remote images were loaded");
    foreach (var target in new[] { "javascript:alert(1)", "file:///C:/private",
                                   "mailto:a@example.com", "", "\\\\server\\file" })
      Check(MarkdownNotes.LinkTarget(target) is null,
            "Markdown accepted a non-web link: " + target);
    Check(MarkdownNotes.LinkTarget("https://example.com")?.Scheme == "https" &&
              MarkdownNotes.LinkTarget("docs/")?.AbsolutePath.EndsWith("/tree/main/docs/") == true,
          "Markdown web or directory links were rejected");
  }
}
