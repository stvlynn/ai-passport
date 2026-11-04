<p align="right">
  <a href="astra-monitor.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Astra Monitor

A pixel-art GitHub dashboard for the [AstraFoundry](https://github.com/AstraFoundry)
organization on FoloToy AI Passport, branch `feature/astra-monitor`. It shows
stars, open issues and pull requests, recent activity, and a per-repository
ranking. The mascot is Lynn ([lynn.stv.pm](https://lynn.stv.pm)), and the
colours follow the AstraFoundry Sunbeam design system (`AstraFoundry/design`):
warm paper and ink, with yellow reserved for today's new stars.

## Pages and controls

| Page | Content |
| --- | --- |
| Overview | Total stars, today's new stars, open issues, open PRs, repository count, a focus repository, and Lynn with a status speech bubble |
| Activity | The 12 most recent star increases, issues, and PRs |
| Repositories | Repositories by stars, with private markers, issue/PR counts, and a square-root star bar |
| Device | Wi-Fi, GitHub, sync, and battery status, plus settings |

| Key | Action |
| --- | --- |
| UP / DOWN | Previous / next page (wraps around) |
| OK click / double click | Select the next / previous item on the page: the focus repository, a list row, or a Device menu item (wraps around) |
| OK long press | Confirm. Device page: run the selected item. Other pages: sync now. Provisioning page: go back |
| UP / DOWN long press | Overview: poke Lynn |

Each key has one meaning on every page, so flipping pages never conflicts with
moving the selection. Every long press gives visible feedback: a card states
that a sync started, why it cannot run (no Wi-Fi, no token), the new value of a
setting, or the sync result.

Device menu: web setup, sync now, sound (off by default), clap to wake, brightness
(100/70/40/20%), screen-off time (1/5/10/30 minutes or never, default 10
minutes), and forget Wi-Fi (needs a second long press within 3 s).

Lynn is animated frame by frame (`main/lynn_anim.c`). When nothing needs
attention she follows an idle routine: she stands, blinks and glances around,
then reads a book (turning pages, smiling at a passage), types on her laptop
(glancing up, hitting Enter with a flourish), sips tea, listens to music on
headphones, stretches, or dozes off (nodding, a sleep bubble, waking with a
start), and returns to standing. Reading and typing come up most often, then tea
and music, then stretching and dozing. She types while a sync is running, dozes
when the backlight dims, and sleeps when it turns off. Mood poses (happy,
reviewing, confused, surprised, asleep) loop small movements instead of holding
one frame.

Reactions play once and then return to the routine: a new star makes her hop, a
merged PR or a total crossing a multiple of 50 stars sets off a party popper, a
new issue gets a magnifying glass, a new PR gets a review, and warnings confuse
her. Poking her waves or hops in turn; poking her while she dozes startles her
awake first.

The backlight dims at half the screen-off time, but no later than 90 s, and
turns off at the screen-off time; with "never" it only dims. The first key press
only wakes the screen. While the screen is dim or off, two claps within 0.7 s
wake it (microphone, can be turned off). New activity wakes the screen, plays
an 8-bit sound if sound is on, slides in a notification card, and Lynn reacts: hop for stars
and merges, surprise for issues, reviewing for PRs.

## Hardware use

| Hardware | Use |
| --- | --- |
| ST7789 display, LVGL | Four pages, provisioning page, notification card, sprite animation |
| Three ADC buttons | Click, double click, and long press |
| ES8311 speaker | Square-wave sound effects synthesized at 16 kHz |
| ES8311 microphone | Double-clap detection while the screen is dim or off |
| CW2017 fuel gauge | Battery percentage in the status bar, voltage on the Device page |
| Wi-Fi STA | HTTPS GraphQL requests and SNTP |
| Wi-Fi SoftAP | Setup hotspot, captive DNS, and web form, only while the setup page is open |
| NVS | Wi-Fi credentials, GitHub token, settings, and the daily star baseline |
| USB Serial/JTAG | Configuration console |

## Setup

1. Flash the merged image `build/FoloToy-AI-Passport-full.bin` at `0x0`. This
   resets NVS, including any saved Wi-Fi and token.
2. Create a fine-grained, read-only GitHub token for the organization
   (Metadata, Issues, and Pull requests read access).
3. On the device: open the Device page, select the first menu item (web setup),
   then long-press OK. The
   device opens a WPA2 hotspot named `AstraMonitor-XXXX` with a random
   8-digit password, and shows both as a Wi-Fi QR code.
4. Scan the QR code with the phone camera, or join the hotspot manually. The
   setup page usually opens on its own; otherwise browse to
   `http://192.168.4.1`. Pick a 2.4 GHz network, enter its password, paste the
   token, and submit.
5. The device tries the new Wi-Fi first. If it connects, the hotspot closes
   and the first sync starts. If it fails within about 20 seconds, the device
   restores the previous Wi-Fi and the page can be submitted again.

The setup page also shows the device's Wi-Fi MAC address, for router
allowlists or DHCP reservations; the console `status` command prints it too.
The FoloToy provisioning mini program only accepts official firmware, so this
application uses a SoftAP web page instead of BLUFI, following the community
note [SoftAP provisioning and resource budgets](reference/phoenixzhc/softap-provisioning-and-resource-budget.md).
Bluetooth is disabled in this firmware.

The USB serial console is an alternative to the web page. Console commands:
`token <value>`, `token clear`, `wifi <ssid> [password]`
(quote an SSID that contains spaces), `wifi clear`, `refresh`, and `status`.
`status` never prints secrets.

Build-time options are under `Astra Monitor` in menuconfig: organization,
refresh interval (default 120 s), time zone (default `CST-8`), the latest
dim delay, the default screen-off time, and an optional fallback token. Do not commit a token in
`sdkconfig.defaults`.

## Data and limits

Each refresh sends two GraphQL requests. The first returns counts and push
times for the 24 repositories with the most stars, plus the remaining rate
limit (about 2.5 KB). The second returns the 3 most recently updated issues
and PRs of the 6 most recently pushed repositories (about 4 KB). The read
buffer allows up to 20 KB.

Fine-grained tokens cannot read who starred a repository: GitHub rejects the
`stargazers` connection and then nulls the whole repository. Organization-wide
`search` returns nothing for them. The device therefore never requests either.
Star activity is derived from star-count increases between syncs ("+3 stars"
on a repository), and issue/PR activity only covers the recently pushed
repositories. "Today" counts the change in the total star
count since the first successful sync after local midnight; removed stars
reduce it.

The 12 px font covers ASCII, GB2312 level-1 hanzi, and common CJK punctuation.
Other characters in issue or PR titles, such as rare hanzi, Japanese kana,
and emoji, are drawn as LVGL placeholder boxes.

## Maintenance

- `python3 tools/gen_app_assets.py` regenerates `main/ui_assets.c` from
  `assets/images/lynn/` and the inline icon art.
- `python3 tools/gen_app_fonts.py --font <fusion-pixel-12px-proportional-zh_hans.ttf>`
  regenerates the fonts after strings change.
- `tools/ui_preview/run.sh` renders every page on the host with the real
  `main/ui.c` and LVGL into `build/ui_preview/`.
- Host tests: `tests/test_gh_model.c`, `tests/test_gh_parse.c`,
  `tests/test_app_nav.c`, `tests/test_sound_synth.c`, and
  `tests/test_portal_proto.c`, which run in
  `./tools/validate.sh --static`.

The baseline hardware-test demo sources (`main/main.c`, `main/demo_*.c`, and
`main/ui_pixel*`) remain in the tree for their host tests, but they are not
part of this firmware build.
