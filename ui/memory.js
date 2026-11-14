// Producer memory: a Markdown file in the user's app data (claude_memory.md)
// that Claude reads on every request and can update with the remember tool.

export const DEFAULT_MEMORY = `# Producer memory

## Mix rules (applied automatically when Auto-mix is on)
- Every song gets two shared effect buses: "Reverb" (Reverb with dry level 0, wet level left at its default ~0 dB) and "Delay" (Delay, feedback around -9 dB, mix fully wet). Feed tracks with set_send instead of putting a reverb on each track.
  - Pads, chords, leads: reverb send around -10 dB; leads also delay send around -14 dB.
  - Bass and kick: no reverb (keeps the low end tight).
- If there is a kick (drum track) and a bass, sidechain_duck the bass to the kick (amount 0.6-0.8). Pads/chords also duck lightly (amount 0.4-0.5) for the pumping feel in synthwave and dance music.
- Only one thing owns the sub (below ~100 Hz): the bass. Put an EQ or Filter high-pass on pads, chords and leads.
- Levels: kick and bass loudest, then lead, then chords/pads around -6 to -9 dB, fx/percussion lower.
- Master bus: gentle EQ, then a Compressor as glue (low ratio). Fade out the last 2 bars unless the song is meant to loop.
- After building or changing a mix, use render_and_analyze once and adjust levels if one part dominates.

## Genre notes
- Synthwave (Tonebox / Magnatron style): 80-118 BPM, minor keys, gated big snare, chorus on pads and bass, arpeggiated 16ths, saw leads with delay.
- Drum and bass: 170-176 BPM, rolling 16th basslines split across a sub track and a mid/reese track, two-step drums, sidechain the bass hard.

## User preferences
(Claude adds notes here when the user states a preference.)
`;

export async function loadMemory(native) {
  const text = await native.readMemory();
  if (typeof text === "string" && text.trim()) return text;
  await native.writeMemory(DEFAULT_MEMORY);
  return DEFAULT_MEMORY;
}

export const MEMORY_TOOLS = [
  {
    name: "remember",
    description: "Save a lasting note to producer memory (a user preference, a mix rule that worked, a sound they liked). Appended under '## User preferences' unless section is given.",
    input_schema: {
      type: "object",
      properties: {
        note: { type: "string" },
        section: { type: "string", description: "Heading to add under, e.g. 'Mix rules' or 'Genre notes'" },
      },
      required: ["note"],
      additionalProperties: false,
    },
  },
];

export function appendNote(memory, note, section = "User preferences") {
  const heading = `## ${section}`;
  const line = `- ${note.trim()}`;
  if (!memory.includes(heading)) return `${memory.trimEnd()}\n\n${heading}\n${line}\n`;
  const start = memory.indexOf(heading) + heading.length;
  const nextHeading = memory.indexOf("\n## ", start);
  const end = nextHeading < 0 ? memory.length : nextHeading;
  const body = memory.slice(start, end).replace("(Claude adds notes here when the user states a preference.)\n", "");
  return memory.slice(0, start) + body.trimEnd() + "\n" + line + "\n" + memory.slice(end);
}
