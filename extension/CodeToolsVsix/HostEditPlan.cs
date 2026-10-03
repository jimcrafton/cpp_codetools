using System.Collections.Generic;

namespace CodeToolsVsix
{
    /// <summary>One edit as native sends it: start/length are UTF-16 indices into the text it was
    /// planned against - the same coordinates ITextBuffer uses, so no conversion is needed.</summary>
    internal readonly struct Utf16Edit
    {
        public Utf16Edit(int start, int length, string text)
        {
            Start = start;
            Length = length;
            Text = text;
        }

        public int Start { get; }
        public int Length { get; }
        public string Text { get; }
    }

    /// <summary>Pure (VS-free) check of a whole plan before any of it is applied.</summary>
    internal static class HostEditPlan
    {
        private static bool SplitsSurrogatePair(string text, int pos)
        {
            return pos > 0 && pos < text.Length && char.IsHighSurrogate(text[pos - 1]) && char.IsLowSurrogate(text[pos]);
        }

        /// <summary>Validates edits against text - in range, no overlap, no start/end between the two
        /// halves of a surrogate pair - and returns them sorted by start (stable, so insertions at
        /// the same position keep their given order). False on any problem: apply nothing.</summary>
        public static bool TryOrder(string text, IReadOnlyList<Utf16Edit> edits, out List<Utf16Edit> ordered)
        {
            var order = new List<int>(edits.Count);
            for (int i = 0; i < edits.Count; i++)
            {
                order.Add(i);
            }
            // List.Sort isn't stable: break ties by original index.
            order.Sort((a, b) =>
            {
                int byStart = edits[a].Start.CompareTo(edits[b].Start);
                return byStart != 0 ? byStart : a.CompareTo(b);
            });

            var result = new List<Utf16Edit>(edits.Count);
            int previousEnd = 0;
            foreach (int i in order)
            {
                Utf16Edit edit = edits[i];
                long end = (long)edit.Start + edit.Length;
                if (edit.Start < 0 || edit.Length < 0 || end > text.Length || edit.Start < previousEnd ||
                    SplitsSurrogatePair(text, edit.Start) || SplitsSurrogatePair(text, (int)end))
                {
                    ordered = null;
                    return false;
                }
                previousEnd = (int)end;
                result.Add(edit);
            }
            ordered = result;
            return true;
        }
    }
}
