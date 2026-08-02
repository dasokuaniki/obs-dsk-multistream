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

- UI asset: `data/ui/kick-icon-green.png`
- Source: KICK Brand Hub, Icons collection
  <https://brandfolder.com/s/rn8r76txqxvc4vcjhf6km6w>
- Official brand resources: <https://about.kick.com/brand>
- Developer terms: <https://dev.kick.com/terms-of-service>
- Retrieved: 2026-08-02
- SHA-256: `C69A9134C1DAB882B09EC0D37EC8B0C27CE0060C49958584A2A3E976E0BB5A10`
- Usage: the official Green Icon PNG is displayed only to identify Kick as the
  user-selected streaming destination. It is resized with its original aspect
  ratio and is not cropped, recolored, reshaped, or used as DSK branding.
- License: the image and Kick marks remain property of Kick or its licensors and
  are not licensed under DSK Multistream's GPL-2.0-or-later license. See
  `docs/third-party-notices.md`.

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
