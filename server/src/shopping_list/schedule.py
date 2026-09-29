"""When the device should next sync.

The server owns the schedule and tells the device, in every sync response, how long to wait before
the next one (`next_sync_in_s`). That keeps the device free of clocks, timezones and settings: it just
sleeps for the number it was given, and changing the schedule needs no reflash.

A schedule is two preset names, one for weekdays and one for weekends (Saturday and Sunday). Presets
live here in code; each is a set of time windows plus how often to sync inside them. Outside every
window the device is left alone.
"""

from dataclasses import dataclass
from datetime import UTC, datetime, timedelta, tzinfo
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

# Bounds on what the device is ever told to wait. The cap means a device is never out of touch for
# longer than this even if the schedule says "nothing until Monday": it wakes, asks again, and is told
# to keep waiting. The floor stops a sync just before a window opens from scheduling a near-instant
# repeat.
MIN_DELAY_S = 30
MAX_DELAY_S = 12 * 60 * 60

MINUTES_PER_DAY = 24 * 60
WEEKEND_DAYS = (5, 6)  # datetime.weekday(): Saturday, Sunday


@dataclass(frozen=True)
class Preset:
    id: str
    name: str
    description: str
    interval_minutes: int
    # (start, end) as minutes since midnight, end exclusive. A sync happens only when the current
    # time is inside a window, so a window ending at 09:00 last syncs at 08:00 when hourly.
    windows: tuple[tuple[int, int], ...]

    def interval_at(self, minute_of_day: int) -> int:
        """Minutes between syncs at this time of day, or 0 when the device should stay quiet."""
        for start, end in self.windows:
            if start <= minute_of_day < end:
                return self.interval_minutes
        return 0

    def syncs_per_day(self) -> int:
        count = 0
        minute = 0
        while minute < MINUTES_PER_DAY:
            interval = self.interval_at(minute)
            if interval:
                count += 1
                minute += interval
            else:
                minute += 1
        return count


def _hm(hour: int, minute: int = 0) -> int:
    return hour * 60 + minute


PRESETS: tuple[Preset, ...] = (
    Preset(
        id="work",
        name="Before and after work",
        description="Hourly 07:00 to 09:00 and 17:00 to 22:00. Quiet the rest of the day.",
        interval_minutes=60,
        windows=((_hm(7), _hm(9)), (_hm(17), _hm(22))),
    ),
    Preset(
        id="day",
        name="Daytime",
        description="Every 30 minutes from 07:00 to 22:00. Quiet overnight.",
        interval_minutes=30,
        windows=((_hm(7), _hm(22)),),
    ),
    Preset(
        id="saver",
        name="Battery saver",
        description="Hourly from 08:00 to 20:00. Quiet overnight.",
        interval_minutes=60,
        windows=((_hm(8), _hm(20)),),
    ),
    Preset(
        id="always",
        name="Always fresh",
        description="Every 15 minutes, all day and night.",
        interval_minutes=15,
        windows=((0, MINUTES_PER_DAY),),
    ),
)
PRESETS_BY_ID = {p.id: p for p in PRESETS}


@dataclass(frozen=True)
class Schedule:
    weekday: str
    weekend: str
    # When set, weekends follow the weekday preset and `weekend` is kept only so it isn't lost when
    # someone unticks the box again.
    weekend_same: bool

    def preset_for(self, weekday: int) -> Preset:
        if weekday in WEEKEND_DAYS and not self.weekend_same:
            return PRESETS_BY_ID[self.weekend]
        return PRESETS_BY_ID[self.weekday]


DEFAULT_SCHEDULE = Schedule(weekday="day", weekend="day", weekend_same=True)


def resolve_timezone(name: str) -> tzinfo | None:
    """The timezone for a `SHOPPING_LIST_TIMEZONE` value; None (the server's local time) if empty.
    Raises at startup rather than silently scheduling in the wrong zone."""
    if not name:
        return None
    try:
        return ZoneInfo(name)
    except (ZoneInfoNotFoundError, ValueError) as e:
        raise ValueError(f"SHOPPING_LIST_TIMEZONE {name!r} is not a known timezone") from e


def _interval_at(schedule: Schedule, when: datetime, tz: tzinfo | None) -> int:
    local = when.astimezone(tz)
    return schedule.preset_for(local.weekday()).interval_at(local.hour * 60 + local.minute)


def next_sync_delay(schedule: Schedule, now: datetime, tz: tzinfo | None = None) -> int:
    """Seconds the device should wait before its next sync, given that it is syncing at `now`.

    Inside a window that is one interval from now, unless that lands outside the window, in which case
    it is the moment the next window opens. Stepping is in UTC so a daylight-saving change can't skip
    or repeat a minute; only the lookup converts to local time. `tz` of None means the server's local
    timezone.
    """
    start = now.astimezone(UTC).replace(second=0, microsecond=0)
    interval = _interval_at(schedule, start, tz)
    candidate = start + timedelta(minutes=interval or 1)
    # No need to look further than the cap: past it the answer is MAX_DELAY_S whatever we find.
    for _ in range(MAX_DELAY_S // 60 + 1):
        if _interval_at(schedule, candidate, tz):
            break
        candidate += timedelta(minutes=1)
    else:
        return MAX_DELAY_S
    delay = int((candidate - now).total_seconds())
    return max(MIN_DELAY_S, min(MAX_DELAY_S, delay))
