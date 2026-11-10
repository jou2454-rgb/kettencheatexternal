# bycmd — Standoff 2 1.1.0, профиль v17

Сборка не запускалась на устройстве. Всё ниже проверено статически: оффсеты
по `dump.cs`/`script.json` (dump x64 1.1.0), код — syntax-check clang 18 на
Linux с заглушками Android-заголовков. Запуск на Infinix GT 30 Pro /
Xiaomi Pad 7 и поведение ESP на живой игре **не подтверждены**.

## Что сверено с дампом

TypeInfo (script.json) — все совпали со старым `v100`:

| Имя | RVA | Класс |
|---|---|---|
| registry | 0xCD32028 | nba |
| store | 0xCD303E8 | ibc |
| player | 0xCD32658 | odq |
| transforms | 0xCD31E68 | mtc |
| model | 0xCD2DDE8 | bis |
| stats | 0xCD360F0 | ydm |
| weapon_part | 0xCD34CE0 | uyg |
| camera | 0xCD1F798 | UnityEngine.Camera |
| ios | 0xCD36248 | yjp |
| Camera.get_main | 0x6BE9604 | метод |

Новые, добавлены в `v100`:

| Имя | RVA | Класс |
|---|---|---|
| presentation | 0xCD2E320 | chc (компонент игрока в `cpa.pjy[]`) |
| animation_data | 0xCD28488 | PlayerAnimationData |
| camera_main (controller) | 0xCD311F8 | kmc : cgy |
| camera_arms (controller) | 0xCD357B0 | wot : cgy |
| camera_state | 0xCD35080 | vqn |
| bor | 0xCD2DEF0 | bor |
| kgd | 0xCD31080 | kgd |

Поля (dump.cs): `wwv` +0x10 id, +0x18 world, +0x20 cpa, +0x28 nzk, +0x44..46
флаги; `odq` +0x68 mtc, +0x70 ydm, +0x78 vqn, +0x80 dvp, +0x90 team;
`mtc` +0x28/+0x30 Transform; `ydm` +0x10 hp, +0x14 max hp (ushort);
`yjp` +0x38 kgd, +0x50/+0x58 Transform; `kgd` +0x10/+0x20; `vqn` +0x10/+0x18 bor;
`bor` +0x10 ValueTransform{Position, EulerAngles}, +0x48/+0x4C float;
`chc` +0xA0 PlayerAnimationData; `PlayerAnimationData` +0x90 ArmsCamera (bor),
+0x100 MainCamera (bor), +0xA4 IsFirstPerson; `cgy` +0x10 bor, +0x20 Camera.

Ошибка старого кода: `nzk` — это не `List`, а `+0x20 cew[]`. Fallback по
частям читал мусор. Исправлено (`player::transform_candidates`).

## Камера: откуда теперь берутся матрицы

Старый код искал камеру эвристиками (декод `Camera.get_main`, обход графа,
имена классов) и чаще всего жил на синтетической камере из `yjp`/`vqn` с
FOV 60. Отдача и поворот в такой камере не совпадают с рендером — отсюда
«боксы уезжают вверх при стрельбе» и отставание при вращении.

Новая цепочка (`src/game/camera.hpp`):

```
локальный odq -> cpa(+0x20) -> pjy[] -> chc -> PlayerAnimationData(+0xA0)
    +0x100 bor MainCamera, +0x90 bor ArmsCamera
kmc / wot (найдены фоновым сканером по klass) : +0x10 bor, +0x20 Camera
    контроллер, у которого +0x10 == MainCamera  ->  Camera -> m_CachedPtr
    -> нативная Camera -> view/projection (+0x70/+0xB0, pattern-проверка)
```

Это матрицы, которыми GPU рисовал кадр: отдача, присед, FOV прицела —
всё внутри. Приоритет источников: `main-native` → `kmc-native` →
`main-bor` (синтетика из MainCamera ValueTransform) → `vqn-bor` →
`ios-transform` → `heap-camera`. Текущий источник виден в меню
(Settings → строка `camera:`) и в логе `[CAMERA] selected=... kind=...`.

Гипотезы, которые подтвердит только лог:
- `kmc` = мировая камера, `wot` = камера рук (по интерфейсам `etx`/`nri`
  и использованию в `npr`/`lcj`). Если наоборот — код всё равно выбирает
  контроллер по совпадению `bor` с `MainCamera`, а не по классу.
- `bor+0x48` или `+0x4C` — FOV (константа `zolb = 60` в том же классе).

## Лаг при вращении

- Сканы кучи (реестр, kmc/wot, PlayerAnimationData, Camera) вынесены в
  фоновый поток `scanner`. Поток рендера только валидирует адреса.
- Чтения на кадр сокращены: объект NetObject — одно чтение 0x48 байт,
  HP/maxHP — одно чтение, пара Transform — одно чтение, матрицы камеры —
  два чтения по залоченным оффсетам.
- Поза и HP читаются один раз, после камеры (раньше — дважды, до и после
  медленного поиска камеры).
- `game::valid()` больше не сканирует `/proc` каждые 60 кадров;
  живость — `kill(pid, 0)`.
- Late latch в `main.cpp`: после `eglSwapBuffers` цикл спит почти весь
  период кадра и читает память игры прямо перед рендером. Возраст данных
  на момент композиции — единицы мс вместо целого кадра.

## Запуск на других устройствах

Что сделано:
- 16 KB page size: `APP_SUPPORT_FLEXIBLE_PAGE_SIZES := true` +
  `-Wl,-z,max-page-size=16384`. Старый `eeeww` выровнен на 4 KB и на ядре
  с 16 KB страницами не запускается вообще.
- Лок-файл больше не фатален: пробует `/data/local/tmp/bycmd.lock`,
  `./bycmd.lock`, иначе работает без лока. Старая версия молча выходила с
  кодом 2, если `/data/local/tmp` недоступен.
- Лог-файл `/data/local/tmp/bycmd.log` (или `./bycmd.log`), в начале —
  модель, Android, page size, uid.
- `--diag`: проверяет дисплей, overlay+EGL+ImGui, `/dev/input`, pid игры,
  `libunity`, build-id и выходит. Это то, что нужно запустить другу на
  Xiaomi Pad 7 и прислать `bycmd.log`.
- Выход по SIGINT/SIGTERM с корректным shutdown.

Что **не** трогал: `ANativeWindowCreator.h` и `draw.cpp` (создание
overlay/EGL) оставлены как в исходнике. Если `--diag` покажет
`display info unavailable` или `initGUI_draw failed`, проблема в этом
слое на конкретной прошивке, и её надо смотреть по логу.

## Проверка сборки (fail closed)

`game::verify_build()` читает `NT_GNU_BUILD_ID` из замапленного
`libunity.so` и сравнивает с `v100::libunity_build_id = 69815f2befe135c2`
(из `remap_final`, самой библиотекой не перепроверено). При несовпадении
ESP не запускается, в меню `build: MISMATCH`, в логе ожидаемый и реальный
id. Если note не читается — вторичная проверка: имя класса в
`Camera_TypeInfo` должно быть `Camera`. `--skip-build-check` только для
диагностики.

## Прочее

- HP-бар считает процент от `max_hp`, а не от 100.
- Кэши `name/dead/hp` чистятся по списку живых объектов (раньше росли
  между матчами и могли скрыть живого игрока по старому «dead» адресу).
- Удалён мёртвый код: `is_scoping`, `deep_transform_diagnostics`,
  `scan_player_heap`, `alive`, декод `Camera.get_main`, цепочка по
  именам классов.
- Резолв ника остался эвристикой и подписан как эвристика.

## Сборка и запуск

```
cd bycmd/jni && ndk-build -j4      # NDK r27+ (флаги 16 KB)
adb push ../libs/arm64-v8a/eeeww /data/local/tmp/bycmd
adb shell "su -c 'chmod 755 /data/local/tmp/bycmd && /data/local/tmp/bycmd --diag'"
adb shell "su -c 'cat /data/local/tmp/bycmd.log'"
adb shell "su -c '/data/local/tmp/bycmd'"          # обычный запуск
adb logcat -s bycmd                                   # дублируется в logcat
```

Ключевые строки лога:

```
[INFO] device model=... android=... pagesize=...
[INFO] libunity build-id=... expected=69815f2befe135c2 match=1
[SCAN] pass 1 done: ... controllers=N animation=M
[CAMERA] chain local=... mainBor=0x... armsBor=0x...
[CAMERA] controller locked ... kind=main-native view=+0x70 proj=+0xB0
[CAMERA] selected=... kind=main-native
```

Если `kind` не `main-native` — прислать блок `[CAMERA]` целиком: там
видно, какие контроллеры нашлись и с какими `bor`.

## Не сделано / нужен ввод

- Запуск на устройстве и проверка `main-native` на живой игре.
- Подтверждение build-id `69815f2befe135c2` по самому `libunity.so`
  (хватит `readelf -n libunity.so`).
- Причина незапуска на Xiaomi Pad 7: без `bycmd.log` от `--diag` это
  предположения (16 KB страницы, лок-файл, overlay API прошивки).
