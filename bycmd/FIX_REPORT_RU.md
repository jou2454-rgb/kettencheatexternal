# Standoff 2 — fixed 1.1.0 profile

## Что было не так

### 1. Поиск libunity был прибит к 1.0.0

`get_lib()` проверял два машинных слова по фиксированным RVA:

```cpp
kSignatureA = 0x6327AD0;  expected 0x4ea36f18
kSignatureB = 0x6327C50;  expected 0x6e2adfca
```

Это подписи старой libunity. После обновления игры (новый SHA256
`ba096697...`, Unity 6, ~217 MB) они не совпадают, поэтому `proc::lib`
оставался `0`, и ESP вообще не запускался.

Теперь загрузочная база ищется по ELF:

- разбирается `/proc/<pid>/maps`;
- проверяется ELF64 / AArch64 / ET_DYN;
- `DT_SONAME` читается из `PT_DYNAMIC` и сравнивается с `libunity.so`;
- это работает и для обычного `.so`, и для библиотеки, замапленной из
  `base.apk`, где в maps нет пути `libunity.so`;
- старые сигнатуры удалены.

### 2. Перепутан TypeInfo компонента трансформа

Было:

```cpp
transforms_typeinfo = 0xCD2BB30; // это UnityEngine.Transform_TypeInfo
```

`transform_candidates()` искал этот класс среди частей игрока
(`nzk`/parts), но `UnityEngine.Transform` там не лежит. Поэтому
позиции брались только из `bis` (если он вообще попадал в parts), а
часть игроков/раундов оставалась без координат.

Стало:

```cpp
transforms_typeinfo = 0xCD31E68; // mtc_TypeInfo, компонент игрока
```

У `mtc` поля `+0x28/+0x30` — два `Transform`, ровно то, что читает
`transform_candidates()`.

### 3. Перепутан TypeInfo WeaponPart

Было:

```cpp
weapon_part_typeinfo = 0xCD33B78; // ruz_TypeInfo (DropItem)
```

`ruz` — это выпавший предмет с `WeaponId`/`WeaponMetaData`, а не
`WeaponPart`. Код дополнительно читал `part + 0x40` как указатель на
managed-трансформ, хотя у `ruz` по этому смещению ничего похожего нет.

Стало:

```cpp
weapon_part_typeinfo = 0xCD34CE0; // uyg_TypeInfo (WeaponPart)
```

Ветка «live aim» через `+0x40` удалена: она всё равно давала мусор.
Ориентация камеры теперь берётся из `yjp + 0x38 -> kgd` (кватернионы
`+0x10/+0x20`), а позиция — из `yjp + 0x50/+0x58`.

### 4. Устаревший RVA Camera.get_main

Было:

```cpp
native_camera_get_main = 0x54C1338; // 1.0.0
```

В dump.cs 1.1.0:

```text
// RVA: 0x6BE9604 ... public static Camera get_main()
```

Стало `0x6BE9604`. Это возвращает рабочий путь к native-камере, когда
она есть; если матрицы не находятся, остаётся transform-fallback.

### 5. Камера выбиралась по расстоянию, а не по проекции

Раньше кандидат в 2–10 метрах от игрока получал +80/+100 и мог
победить настоящую камеру, даже если ни один враг в неё не проецируется.
Отсюда «плавающие»/смещённые боксы и выбор камеры от preview-объекта.

Теперь:

- кандидат без единого спроецированного тела отбрасывается;
- вес проекции доминирует: `front * 10`, `onscreen * 50`;
- расстояние даёт только +5;
- камера без `front > 0` не выбирается.

### 6. Флаг локального игрока

У `wwv` три соседних byte-флага: `+0x44`, `+0x45`, `+0x46`
(`IsServer` / `IsLocal` / `IsLocalOwned` в старом порядке). После
обфускации порядок не гарантирован.

`players()` теперь проверяет все три смещения на живом наборе и
выбирает то, где ровно один объект помечен локальным. Старое `+0x44`
остаётся первым кандидатом, поэтому для текущего дампа поведение не
меняется, но при следующем ремапе ESP не отвалится целиком.

## Исправления падений

- `init_egl()`: проверка `ANativeWindowCreator::Create()`, `eglInitialize`,
  `eglChooseConfig`, `eglGetConfigAttrib`, `ANativeWindow_setBuffersGeometry`,
  `eglCreateContext`, `eglCreateWindowSurface`, `eglMakeCurrent`.
- `drawEnd()` не вызывает GL при потерянном/неинициализированном surface.
- `shutdown()` не делает `ANativeWindow_release(nullptr)`.
- `ImGui_init()` проверяет, что все четыре шрифта созданы; иначе меню
  падало на `fontBold->FontSize` / `fontMedium->FontSize`.
- `Touch::init()` проверяет `opendir("/dev/input")`; раньше `readdir(nullptr)`
  давал SIGSEGV на устройствах без доступа к `/dev/input`.
- touch-поток не крутится в busy-loop при `read() == 0`.
- `ANativeWindowCreator`:
  - проверяются vendor-символы `libgui`/`libutils`; null-функции больше не
    вызываются;
  - `SurfaceControl::GetSurface()` не превращает null в `+sizeof(max_align_t)/2`;
  - `SurfaceComposerClient` имеет `valid` и не вызывает
    `SurfaceComposerClient__Constructor` при недоступном API;
  - `Destroy()` больше не перепутан (`if (found) return;` был багом);
  - версия Android парсится без `std::stoi` (preview-ROM с "Baklava" не
    роняет процесс).
- `main`: до 2 секунд ждёт валидный `displayInfo`, иначе выходит с логом,
  а не создаёт overlay 0x0.

## ESP привязан к модели, а не к снимку до скана камеры

Раньше `visuals::draw()` сначала читал позиции всех игроков, потом мог
потратить десятки миллисекунд на `camera_candidates()`/поиск камеры, и
только после этого рисовал боксы по **старым** координатам. На ходьбе это
выглядело как «ESP догоняет игрока», на стрельбе — как смещение боксов
относительно модели.

Что изменено:

- позиция игрока теперь берётся напрямую из `odq + 0x68` (`aiyh -> mtc`),
  то есть из компонента, который двигает саму модель, а не из кэша частей;
- `player::pose()` читает оба `Transform` (`+0x28/+0x30`) и отдаёт
  реальные `feet/head`; если вертикальная разница не похожа на тело,
  остаётся старый fallback `+1.67`;
- после выбора камеры поза каждого врага **перечитывается ещё раз** перед
  проекцией, поэтому бокс рисуется по координатам текущего кадра;
- для native-камеры матрицы `view/projection` перечитываются прямо перед
  рисованием; для transform-fallback камера пересобирается из живых
  кватернионов `yjp`;
- добавлен ещё один источник камеры: `odq+0x78` (`vqn`) → `bor` →
  `ValueTransform {Position, EulerAngles}`. Это камера из
  animation/aim-пайплайна, она меняется при присяде, повороте и отдаче.
  Euler переводится в кватернион в порядке Unity ZXY, FOV берётся из
  `bor+0x48/+0x4C`, если там лежит разумное значение;
- выбор камеры получил hysteresis: текущая камера получает небольшой
  бонус, поэтому при повороте не переключается между native/state/yjp
  каждый кадр и не «рвётся»;
- поиск камеры (`discover_camera_*`) теперь выполняется не каждый кадр,
  а раз в 2 секунды или при пустом списке кандидатов (повтор не чаще
  2 раз в секунду) — это убирает задержку между чтением игрока и
  отрисовкой;
- `detect_pair()` расширен до `+0x2C0`, а `probe_camera_pointer()` теперь
  проверяет `m_CachedPtr` managed-камеры (`+0x10`), а не только сам
  объект. Это нужно, чтобы находить настоящую `Camera.main` и её
  projection при скопе, а не постоянно сидеть на synthetic fallback;
- native-камера получает приоритет над synthetic fallback, если обе
  видят цели;
- если native view не выиграл выбор, но native projection от камеры,
  которая видит цели, доступна, она подставляется в fallback. Это
  сохраняет реальный FOV при скопе: фиксированные 60° больше не
  растягивают/сжимают боксы;
- ширина бокса уменьшена с `0.25 * height` (полная ширина `0.5h`) до
  `0.21 * height` (полная `0.42h`) — ближе к реальному силуэту игрока.

## Диагностика в logcat

`diag::log()` теперь дублирует строки в logcat с тегом `bycmd`. Раньше
проект писал только в stdout, поэтому в присланном `logcat.txt` не было
ни одной строки ESP.

Смотреть так:

```sh
adb logcat -s bycmd
```

Ключевые строки:

```text
[bycmd][CAMERA] pair obj=... view=+0x70 proj=+0xB0 fov=...
[bycmd][CAMERA] selected=... view=+0x... proj=+0x...
[bycmd][PLAYER] ... pos=(...) head=(...) body=...
[bycmd][HEALTH] object=... hp=... max=... valid=... stats=...
[bycmd][ESP] ESP frame ... projected=...
```

Если `selected` постоянно имеет `view=+0x-1 proj=+0x-1`, ESP работает на
synthetic-камере с FOV 60 — именно там скоп и отдача будут плавать.
Если `pair` не появляется вообще, native-камера не находится, и нужно
смотреть `probe`/`main-chain` строки.

## Мёртвые игроки

- `health_info()` проверяет `stats`-класс, `hp`, `max_hp` и отсекает
  sentinel `0xFFFF`, `max_hp == 0`, `hp > max_hp`;
- `health()` больше не возвращает «полный HP» при `hp == max == 0xFFFF`;
- добавлен короткий dead-latch (1.5 с): если игрок один раз прочитался
  мёртвым, его бокс не возвращается от мигающего/устаревшего `hp`;
- `pose()` больше не использует parts-fallback, если у `odq` нет живого
  `aiyh`; у мёртвого/деспавненного NetObject трансформ обычно уже пуст;
- поза и HP перечитываются непосредственно перед рисованием, поэтому
  смерть в середине кадра сразу убирает бокс;
- `players()` больше не подмешивает cache-only записи, когда в registry
  уже есть нормальный снимок матча (`current_objects >= 2`). Раньше
  убитый игрок исчезал из registry, но ещё 2.5 секунды висел в кэше со
  старыми HP — отсюда бокс «в пустом месте, где был убит враг».

## Проверка

Сборка:

```text
NDK 27.3.13750724
ndk-build -j4
EXIT=0
```

Артефакт:

```text
project/bycmd/libs/arm64-v8a/eeeww
ELF64, AArch64, PIE (DYN), ~582 KB
```

Что проверено статически:

- все TypeInfo-адреса сверены с `script.json` (Address + Name);
- `Camera.get_main` сверен с `dump.cs` (RVA 0x6BE9604);
- поля `mtc`/`yjp`/`uyg`/`odq`/`ydm` сверены с `offsets.hpp` и `dump.cs`;
- проект собирается без ошибок.

Что **не** проверено в рантайме: на этом ПК нет подключённого Android-устройства
(`adb devices` пуст), поэтому поведение overlay/ESP на живой игре не
подтверждено. Это ограничение, а не «всё работает».

## Что смотреть в логах, если останется проблема

```text
[bycmd][INFO] libunity.so load bias=... (ELF SONAME, N candidates)
[bycmd][INFO] local NetObject flag resolved at +0x45 (old +0x44)
[bycmd][CAMERA] transform fallback ...
[bycmd][CAMERA] selected=... score=... front=... onscreen=...
[bycmd][ESP?] ESP frame players=... alive=... projected=... camera=...
```

Если `libunity.so load bias` не появляется — `get_lib` не нашёл ELF;
нужен `adb logcat`/stdout и список maps процесса.
Если `front=0` у всех кандидатов — проблема в позициях/матрице, а не в
фильтре камеры.

## Остаточный риск

- Нативный layout `TransformHierarchy` (stride 48, `+0x38/+0x40`,
  entries `+0x18`, parents `+0x20`) остаётся Unity-зависимым; он не
  проверялся на живом процессе в этой сессии.
- FOV 60° и eye-height `+1.60` в transform-fallback — эмпирические.
- `player_team = 0x90`, `stats_hp = 0x10`, `stats_max_hp = 0x14`
  взяты из дампа; на рантайме не перепроверялись.
- `get_lib` рассчитан на `DT_SONAME = libunity.so`; для нестандартно
  упакованного APK остаётся pathname-fallback.
