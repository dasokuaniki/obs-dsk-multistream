# Platform brand assets

DSK Multistream uses platform branding only to identify the destination selected by
the user. The product is not affiliated with, sponsored by, or endorsed by any
listed platform.

## YouTube

- UI asset: `data/ui/youtube-icons-2x.png`
- Source: <https://developers.google.com/static/youtube/images/youtube-icons-2x.png>
- Official policy: <https://developers.google.com/youtube/terms/branding-guidelines>
- Retrieved: 2026-07-30
- SHA-256: `9576711E4D997E89EC4F10A10AF84D498C05010485EE02DDBBBABBE44CB63C5A`
- Usage: the unmodified red icon in the official image sheet is rendered at its
  original aspect ratio. The badge links to YouTube Studio as required by the
  YouTube API branding guidelines.

## Twitch

- UI asset: `data/ui/twitch-glitch-purple.png`
- Source package: <https://brand.twitch.com/uploads/Twitch-Brand.zip>
- Official policy: <https://legal.twitch.com/en/legal/trademark/>
- Retrieved: 2026-07-30
- SHA-256: `0A7BC78F69BECD1D6A082B4740D8CC6C096DB99A6F3FA586951A2126007434B4`
- Usage: the official Twitch Purple Glitch image is resized without changing its
  color, proportions, or design. The badge links to the Twitch Creator Dashboard.

## Kick

- Official brand resources: <https://about.kick.com/brand>
- Public UI: neutral monochrome destination glyph drawn by DSK; no Kick image is
  included in source or installer payloads.
- Reason: a third-party repository's GPL license governs copyright in its code
  and files but does not grant permission to redistribute another company's
  trademark. The first-party toolkit is linked for reference and is not bundled.

## TikTok in public builds

TikTok's developer design guidelines state that its logos, icons, symbols, and
designs may not be used without prior written permission. Public source and
installer payloads therefore do not contain a TikTok logo. The public fallback
is a monochrome generic music-note glyph without TikTok colors or trade dress.

The plugin recognizes an optional runtime-only
`data/ui/tiktok-personal.png`. It is intentionally absent from this repository
and every public package. It may only be supplied in an authorized personal
environment; its absence is the expected public configuration. The TikTok
destination remains a manual Custom RTMP target.
