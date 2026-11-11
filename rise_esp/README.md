# rise_esp

Чистый ESP с нуля для rise 1.0.0F5. Математика и отрисовка полностью готовы и проверены тестами.
Игровой слой (адреса) заполнен там, где видно из дампа, остальное помечено `FILL`.

## Что готово

- `include/esp/vec.hpp`, `include/esp/mat.hpp`: векторы, матрицы 4x4, `perspective`, `lookAt`, `worldToScreen`.
- `include/esp/esp.hpp`, `src/esp.cpp`: из снимка игроков получаются примитивы: бокс, полоска хп, ник, скелет.
  Функция без побочных эффектов, рисует платформа.
- `include/mem/memory.hpp`, `src/memory.cpp`: чтение чужой памяти через `/proc/<pid>/mem`, только чтение.
- `game/rise_layout.hpp`: смещения из дампа (с источником у каждой строки) и `FILL`.
- `game/rise_reader.cpp`: сборка снимка кадра, пока `FILL` не заполнены, вернёт список того, что нужно.
- `tests/`: математика, сборщик ESP, чтение памяти (проверяют на хосте).

## Соглашения

- Матрицы column-major, как `UnityEngine.Matrix4x4`: `m[col * 4 + row]`, вектор столбцом.
- `clip = P * V * (x, y, z, 1)`, экран: начало слева сверху, y вниз.
- Голова = ноги + 1.8 м. Это допущение, его можно поменять в `rise_reader.cpp` (`kHeadHeight`).

## FILL: что нужно найти и как

Все нули в `game/rise_layout.hpp`. Без них `readFrame` не работает.

1. `kFillManagerInstance` — RVA статического слота, в котором лежит указатель на `PlayerManager`.
   - в `script.json` найди методы `Axlebolt.Standoff.Player.PlayerManager` (геттер инстанса, `Instance` из `LazySingleton`);
   - в IDA по адресу геттера посмотри, откуда читается статическое поле (`ADRP` + `LDR`), вычти базу модуля.
2. `kFillTransformPos` — смещение мировой позиции внутри нативного `Transform`.
   - стой на месте и снимай кандидатов: координаты, которые совпадают с твоей позицией в игре, и есть нужные;
   - или в IDA найди `Transform_CUSTOM_get_position` и посмотри, что он читает.
   - если позиция лежит за иерархией (несколько шагов), перепиши `transformPosition()`.
3. `kFillPcHealth`, `kFillPcMaxHealth` — смещения хп и максимума внутри `PlayerController` (в дампе не видны по имени).
   - получи урон и смотри, какой `int32` падает со 100.
4. `kFillCamView`, `kFillCamProj` — смещения двух матриц 4x4 (float) в нативной `Camera`.
   - вращай камеру: матрица, которая меняется, это view;
   - в проекции `m[0] = f / aspect`, `m[5] = f`, поэтому её легко опознать.

## Сборка и запуск

```
make          # rise_esp, rise_esp_test, test_mem
make test     # тесты
./rise_esp <pid> libil2cpp.so 1920 1080    # нужен root
```

Если `FILL` не заполнены, программа выведет, что осталось.

## Статус

Тесты математики и сборщика ESP проходят. Чтение памяти проверено на собственном процессе.
На игре и устройстве ничего не проверено, адреса `FILL` ещё не найдены.

## Обновление после бинарника (libil2cpp.so, build 02b5a931...)

- бинарник совпадает с дампом: 95% случайно взятых адресов методов из `script.json` начинаются как настоящий код.
- позиция игрока берётся из managed-памяти, нативный transform не нужен:
  `PlayerController +0x78` (MovementController) -> `+0xA8` (translationData) -> `+0x44` (Vector3, корень персонажа).
  это то, что `MovementController.HAEEFHHBFEEGFHH` передаёт в `Transform.set_position`.
- хп: кандидат `float` на `PlayerController +0x5C`. не проверено, калибровать получением урона.
- всё ещё FILL: адрес инстанса `PlayerManager` (статическое поле `LazySingleton<T>.GFEDEBEDGGFGCHA`, путь через static fields класса),
  камера (view/proj: нужна поза камеры и FOV, кандидаты `PlayerFPSCamera` Vector2 +0xB8/+0xC0 и `CameraScopeZoomer._fov` +0x30),
  максимальное хп.
