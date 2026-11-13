using System;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using Markdig;
using Markdig.Extensions.Tables;
using Markdig.Syntax;
using Markdig.Syntax.Inlines;
using WpfList = System.Windows.Documents.List;
using WpfTable = System.Windows.Documents.Table;
using WpfTableRow = System.Windows.Documents.TableRow;
using WpfTableCell = System.Windows.Documents.TableCell;

namespace lollipop;

internal static class MarkdownNotes {
  private static readonly MarkdownPipeline Pipeline = new MarkdownPipelineBuilder()
                                                          .UsePipeTables()
                                                          .UseAutoLinks()
                                                          .UseEmphasisExtras()
                                                          .DisableHtml()
                                                          .Build();
  private static readonly Brush Muted = new SolidColorBrush(Color.FromRgb(201, 197, 209));
  private static readonly Brush Accent = new SolidColorBrush(Color.FromRgb(255, 145, 195));
  private static readonly Brush CodeBackground = new SolidColorBrush(Color.FromRgb(45, 38, 52));

  internal static Uri? LinkTarget(string? target) {
    if (string.IsNullOrWhiteSpace(target))
      return null;
    if (!Uri.TryCreate(target, UriKind.RelativeOrAbsolute, out var uri))
      return null;
    if (!uri.IsAbsoluteUri) {
      var kind = target.EndsWith('/') ? "tree" : "blob";
      uri = new Uri(new Uri("https://github.com/kxrxh/lollipop-chainsaw-recomp/" + kind + "/main/"),
                    uri);
    }
    return uri.Scheme is "https" or "http" ? uri : null;
  }

  internal static FlowDocument Render(string markdown, Action<Uri> openLink) {
    var document = new FlowDocument { FontFamily = new FontFamily("Segoe UI"),
                                      FontSize = 14,
                                      Foreground = Muted,
                                      Background = Brushes.Transparent,
                                      PagePadding = new Thickness(0),
                                      ColumnWidth = double.PositiveInfinity,
                                      LineHeight = 22 };
    Blocks(Markdig.Markdown.Parse(markdown, Pipeline), document.Blocks, openLink);
    return document;
  }

  private static void Blocks(ContainerBlock source, BlockCollection output, Action<Uri> openLink) {
    foreach (var block in source) {
      switch (block) {
      case HeadingBlock heading:
        var title = new Paragraph { FontSize =
                                        heading.Level switch {
                                          1 => 25,
                                          2 => 20,
                                          _ => 16
                                        },
                                    FontWeight = FontWeights.SemiBold, Foreground = Brushes.White,
                                    Margin = new Thickness(0, output.Count == 0 ? 0 : 18, 0, 10) };
        Inlines(heading.Inline, title.Inlines, openLink);
        output.Add(title);
        break;
      case ParagraphBlock paragraph:
        var text = new Paragraph { Margin = new Thickness(0, 0, 0, 12) };
        Inlines(paragraph.Inline, text.Inlines, openLink);
        output.Add(text);
        break;
      case ListBlock list:
        var items = new WpfList { MarkerStyle = list.IsOrdered ? TextMarkerStyle.Decimal
                                                               : TextMarkerStyle.Disc,
                                  Padding = new Thickness(22, 0, 0, 0),
                                  Margin = new Thickness(0, 0, 0, 12) };
        if (list.IsOrdered && int.TryParse(list.OrderedStart, out var start) && start > 0)
          items.StartIndex = start;
        foreach (ListItemBlock item in list) {
          var listItem = new ListItem { Margin = new Thickness(0, 0, 0, 5) };
          Blocks(item, listItem.Blocks, openLink);
          foreach (var child in listItem.Blocks)
            child.Margin = new Thickness(0);
          items.ListItems.Add(listItem);
        }
        output.Add(items);
        break;
      case QuoteBlock quote:
        var section =
            new Section { BorderBrush = Accent, BorderThickness = new Thickness(3, 0, 0, 0),
                          Padding = new Thickness(14, 0, 0, 0),
                          Margin = new Thickness(0, 6, 0, 12) };
        Blocks(quote, section.Blocks, openLink);
        output.Add(section);
        break;
      case CodeBlock code:
        output.Add(new Paragraph(new Run(code.Lines.ToString())) {
          FontFamily = new FontFamily("Consolas"), FontSize = 12, Background = CodeBackground,
          Padding = new Thickness(12), Margin = new Thickness(0, 4, 0, 12)
        });
        break;
      case ThematicBreakBlock:
        output.Add(new Paragraph { BorderBrush = CodeBackground,
                                   BorderThickness = new Thickness(0, 1, 0, 0),
                                   Margin = new Thickness(0, 8, 0, 12) });
        break;
      case Markdig.Extensions.Tables.Table table:
        var grid = new WpfTable { CellSpacing = 0, Margin = new Thickness(0, 4, 0, 12) };
        var group = new TableRowGroup();
        foreach (Markdig.Extensions.Tables.TableRow row in table) {
          var gridRow = new WpfTableRow { FontWeight = row.IsHeader ? FontWeights.SemiBold
                                                                    : FontWeights.Normal };
          foreach (Markdig.Extensions.Tables.TableCell cell in row) {
            var gridCell =
                new WpfTableCell { Padding = new Thickness(10, 6, 10, 6),
                                   BorderBrush = CodeBackground, BorderThickness = new Thickness(1),
                                   ColumnSpan = Math.Max(1, cell.ColumnSpan) };
            Blocks(cell, gridCell.Blocks, openLink);
            gridRow.Cells.Add(gridCell);
          }
          group.Rows.Add(gridRow);
        }
        grid.RowGroups.Add(group);
        output.Add(grid);
        break;
      case ContainerBlock container:
        Blocks(container, output, openLink);
        break;
      }
    }
  }

  private static void Inlines(ContainerInline? source, InlineCollection output,
                              Action<Uri> openLink) {
    if (source is null)
      return;
    foreach (var inline in source) {
      switch (inline) {
      case LiteralInline literal:
        output.Add(new Run(literal.Content.ToString()));
        break;
      case CodeInline code:
        output.Add(new Run(code.Content) { FontFamily = new FontFamily("Consolas"),
                                           Background = CodeBackground });
        break;
      case LineBreakInline lineBreak:
        if (lineBreak.IsHard)
          output.Add(new LineBreak());
        else
          output.Add(new Run(" "));
        break;
      case EmphasisInline emphasis:
        var span = new Span();
        if (emphasis.DelimiterChar == '~')
          span.TextDecorations = TextDecorations.Strikethrough;
        else if (emphasis.DelimiterCount >= 2)
          span.FontWeight = FontWeights.Bold;
        else
          span.FontStyle = FontStyles.Italic;
        Inlines(emphasis, span.Inlines, openLink);
        output.Add(span);
        break;
      case LinkInline link:
        var target = link.IsImage ? null : LinkTarget(link.Url);
        if (target is null) {
          Inlines(link, output, openLink);
          break;
        }
        var hyperlink = new Hyperlink { NavigateUri = target, Foreground = Accent,
                                        ToolTip = target.AbsoluteUri };
        Inlines(link, hyperlink.Inlines, openLink);
        hyperlink.RequestNavigate += (_, args) => {
          args.Handled = true;
          openLink(args.Uri);
        };
        output.Add(hyperlink);
        break;
      case AutolinkInline automatic:
        var uri = LinkTarget(automatic.Url);
        if (uri is null) {
          output.Add(new Run(automatic.Url));
          break;
        }
        var autoLink =
            new Hyperlink(new Run(automatic.Url)) { NavigateUri = uri, Foreground = Accent };
        autoLink.RequestNavigate += (_, args) => {
          args.Handled = true;
          openLink(args.Uri);
        };
        output.Add(autoLink);
        break;
      case ContainerInline container:
        Inlines(container, output, openLink);
        break;
      }
    }
  }
}
