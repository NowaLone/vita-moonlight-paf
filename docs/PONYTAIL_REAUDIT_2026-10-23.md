# Повторный ponytail-audit — 2026-10-23

Новый проход по актуальному коду, а не ревью предыдущего diff. Исходная точка:
`07b1410d47e8820fae063b3f6529556388b337f5`, ветка
`codex/simplify-implementation`, рабочее дерево перед началом чистое.
Предыдущая экономия из REFACTOR_AUDIT.md сюда не включена.

Режим ponytail-audit использован для поиска и ранжирования. Его обычное ограничение
«только отчёт» переопределено явным запросом сопровождающего сразу применять
подтверждённые безопасные изменения. После каждого изменения просмотрен diff,
выполнены указанные ниже non-build проверки и создан отдельный коммит. Push,
сборка, CMake, Make, компиляция ресурсов, упаковка и деплой не запускались.

## Результат и метод подсчёта

- **Выполнено:** 12 независимых изменений, **−140 строк production C/C++**:
  45 добавленных / 185 удалённых, 21 файл в `src/` и `include/`.
- **Тестовый Python-код:** +76 строк, отдельно от экономии production.
  Production вместе с тестовым кодом: −64 строки. Документация считается отдельно.
- **Проверки:** исходные 183 и итоговые **184 unittest-проверки проходят**.
  Добавлен один тест OptionMenu; существующий touch-тест расширен на release.
- **Остаточная потенциальная экономия:** ориентировочно **39 строк**, не выполнена
  и не включена в −140. Новых зависимостей нет; удаления зависимостей не подтверждены.

Строки выполненных находок ниже относятся к **исходному 07b1410**, чтобы можно
было найти удалённый символ через `git show 07b1410:<файл>`. Строки оставшихся
предложений относятся к **166bf56**, последнему коммиту исходников этого прохода.
Порядок — от большей фактической экономии к меньшей. LOC — чистый diff production,
включая обычные разделительные строки; косметическое уплотнение не применялось.

## Подтверждённые и выполненные находки

### 1. [shrink] Общий close/notify OptionMenu — −31 строка

Место: `src/option_menu.cpp:82`, `OptionMenu::OnDismiss`, также OnSettings/OnDelete
и конструктор на строке 139; `include/option_menu.h:21`, единственный caller
`src/pages/page_main.cpp:202`. Коммит **1d337b6**.

Три копии null guard → захват callback/userdata → delete → notify заменены одним
private static helper. Удалены игнорировавшиеся аргументы конструктора plugin/parent;
caller передавал только чтение указателей, без побочных эффектов.
Все три callback остаются зарегистрированы через `bind_decide`; CXML ID, кнопки,
focus и переходы не менялись. Base по-прежнему открывает тот же popup.

```cpp
void OptionMenu::CloseAndNotify(void *userdata, EventType type, int button)
{
    OptionMenu *menu = (OptionMenu *)userdata;
    if (!menu) return;
    EventCb callback = menu->m_cb;
    void *data = menu->m_userdata;
    delete menu;
    if (callback) callback(type, button, data);
}
```

Риск: низкий; важен именно delete-before-callback. Проверки: новый actual-source
тест прошёл **до и после** изменения, 9 сочетаний действия/отсутствующих данных;
3 XML/resource проверки, diff check. На Vita: Dismiss, Delete, Settings, Back,
disabled Delete, возврат focus; сборка обоих профилей остаётся за сопровождающим.

### 2. [delete] Невостребованные API типов/поиска страниц — −31 строка

Место: `include/pages/page.h:5/10/17/22`, Type/GetType/CloseType/free Find;
`src/pages/page.cpp:160/170`, `src/pages/page_stream.cpp:313`, Stream::Instance;
шесть GetType overrides в заголовках страниц/OptionMenu. Коммит **3858f0a**.

У CloseType нет callers; только он читает GetType. У свободного page::Find и
Stream::Instance тоже нет callers. Проверены определения, вызовы, адреса функций,
ресурсы, тесты и профили. Эти классы — собственные классы приложения, не native PAF
интерфейсы; exports.yml экспортирует только module_start. Замена — ничего.
Base::Find/IsOpen по CXML ID, стек, virtual destructor, s_instance для presentation
и настоящие service/backend интерфейсы сохранены.

Риск: низкий, но требуется native C++/vtable проверка полной сборкой обоих профилей.
Проверки: отсутствие оставшихся source/resource/test ссылок, все 183 исходных теста,
diff check. На Vita: Main → Search → Pairing/Apps → Stream → Back/stop и OptionMenu.

### 3. [delete] Недостижимое mDNS orphan merge — −19 строк

Место: `src/backend/legacy/mdns/udp_sniffer_vita.c:125`, merge_ip_entries;
update_sunshine на строке 76. Коммит **6e6b3cc**.

Удалены функция/вызов merge и всегда NULL private аргумент ip. Условию merge нужно
одновременно `!host[0] && target[0] && ip[0]`. После init/deinit всё обнулено;
PTR/SRV создают только непустой host; A обновляет только запись с непустым host;
ready-notify очищает host/target/ip вместе до следующего poll. Других writers,
публичного доступа к таблице или параллельного writer нет. Следовательно, ветка
ничего не делала в достижимом состоянии. Discovery callbacks и bounds checks сохранены.

Риск: низкий; найденное раньше поведение A-before-SRV не переопределялось.
Проверки: 3 mDNS packet/decoder + 9 discovery lifecycle тестов, writer/callback
dataflow review, diff check. Из parser-теста убран только mock вызова удалённого
no-op; проверки PTR/SRV/A/notify и malformed packets сохранены. На Vita: LAN scan,
несколько ПК, завершение/отмена/restart scan; сборка обоих профилей.

### 4. [shrink] Одинаковые CONNECTION_FAILED/CLOSED в Main — −10 строк

Место: `src/pages/page_main.cpp:1160`, Main::OnMoonlightEvent. Коммит **afad87e**.
Соседние case теперь имеют одно прежнее тело: сброс m_connecting, ShowIdleStatus,
активация списка. Содержимое совпадало, порядок вызовов не менялся; native ошибки
по-прежнему принадлежат ConnectionService.

```cpp
case MOONLIGHT_EVENT_CONNECTION_FAILED:
case MOONLIGHT_EVENT_CONNECTION_CLOSED:
    // прежнее общее тело
    break;
```

Риск: низкий. Проверки: source comparison, 10 host-list + 2 foreground-completion
теста, diff check. На Vita: ошибка Connect и обычное закрытие подключения с возвратом
к списку; сборка обоих профилей. Эти source-тесты не исполняют native Main event UI.

### 5. [delete] Неиспользуемые legacy init inputs — −8 строк

Место: `src/backend/legacy/legacy_gamestream.c:1887/1899`, InitArgs/log_level и
legacy_gamestream_init; header:58, caller `src/moonlight/api.cpp:414`;
`src/backend/legacy/mdns/udp_sniffer_vita.c:26`, CACHE_TIMEOUT. Коммит **a23534d**.

log_level только переносился через worker и завершался `(void)log_level`; единственный
caller передавал 0. Удалена вся цепочка аргумента, а также macro без reads. Ни runtime
Debug log/AppSettings, ни admission/version/error проверки не сокращались.

Риск: низкий, internal C signature требует пересборки всех callers. Проверки:
all-repo references, 2 worker + 3 server-trust/grammar + 7 disconnect-admission
тестов, diff check. На Vita: Connect/pair/reconnect и Debug log в обоих профилях.

### 6. [delete] Дублирующий started-флаг Pairing — −8 строк

Место: `src/pages/page_pairing.cpp:20/110/248`, Pairing::StartPairing/OnMoonlightEvent;
`include/pages/page_pairing.h:30`, m_pairing_started. Коммит **da38041**.

Единственный read был в private StartPairing, вызываемом один раз валидным
конструктором. Ни callback, ни CXML, ни runtime hook его повторно не вызывает.
Удалены поле и записи; empty-PIN guard сохранён. Реальную операцию и отмену по-прежнему
держит PairingService/job, ошибка и навигация остаются прежними.

Риск: низкий. Проверки: все callers/регистрация callbacks, 10 pairing-error тестов,
diff check. На Vita: PIN success/error/cancel и возврат на родительскую страницу;
сборка обоих профилей. Тесты не доказывают native page lifecycle.

### 7. [shrink] Одинаковый terminal bookkeeping API — −6 строк

Место: `src/moonlight/api.cpp:216`, on_stream_event, и соседние result branches
в moonlight_api_start_application:692/697. Коммит **e255c79**.
Объединены одинаковые STOPPED/FAILED присваивания и соседние `if (result != 0)`.
События остаются разными; **application_id очищается после emit**, как раньше.
Результаты, conflict payload, admission и cleanup ownership не менялись.

Риск: низкий. Проверки: 14 remote-Quit + 6 adapter-lifecycle + 7 disconnect-admission
тестов, ordering/diff review. На Vita: failed setup, нормальный stop, conflict →
Quit/launch и повторный запуск; сборка обоих профилей.

### 8. [shrink] Общий touch dispatch при release — −6 строк

Место: `src/moonlight/vita_touch.c:33`, release_touch_state. Коммит **575e65b**.
Существующий send_touch_event перенесён выше и переиспользован для DS4 Up;
координаты/pressure те же. Tablet CANCEL_ALL и mouse-release не заменялись.

Риск: низкий. Расширенный actual-source touch-тест прошёл до/после: DS4 Up,
tablet CANCEL_ALL, очистка contact массива; также 2 PS-button теста и diff check.
На Vita: контакт при остановке/возобновлении в DS4/Tablet/Mouse, без зависших
нажатий; сборка обоих профилей. Реальная выборка/транспорт остаются вне теста.

### 9. [delete] Три временных globals input — −6 строк

Место: `src/moonlight/vita_input.c:44/50/55`, s_mouse_acceleration/s_swap_xo/
s_mapping_enabled, private vita_input_load_mapping:806. Коммит **faf7912**.
Значения читались только непосредственно вызываемыми configuration helpers;
передаются аргументами. У mapping helper один caller, ни input worker, ни callback
эти globals не читают. Сохраняемые face mappings, настройки, путь и формат vita.conf,
вызовы/обработка file I/O остаются прежними.

Риск: низкий. Проверки: полное совпадение тела mapping после подстановки имён,
16 сочетаний zero/nonzero аргументов, 2 PS + 10 controller-light + 1 touch тест,
diff check. На Vita: X/O swap, custom/default mapping и mouse acceleration,
Settings apply во время обычного цикла; сборка обоих профилей.

### 10. [shrink] Один bounded ASCII→wchar проход About — −6 строк

Место: `src/moonlight/settings.cpp:29`, build_about_description. Коммит **166bf56**.
Два последовательных прохода заменены одним по concatenated literal. CMake и
fallback определяют MOONLIGHT_GIT_COMMIT строковым литералом в обоих профилях.
Остались unsigned-char cast, 255-character bound и финальный NUL; wchar stdlib
не вводилась, `-fshort-wchar` и native GetString fallback не менялись.

```cpp
const char *text = "Settings are stored by the Vita AppSettings service.\nBuild commit: "
    MOONLIGHT_GIT_COMMIT;
// прежний bounded conversion loop по text
```

Риск: низкий. Одноразовая actual-source интерпретация старого/нового helper совпала
для unknown/12-character/empty/400-character build ID; 10 controller-light/settings
проверок и diff check. На Vita: About description/build ID; сборка обоих профилей.
Интерпретатор проверяет элементы массива, не native wchar ABI.

### 11. [shrink] Приватная forwarding-обёртка frame release — −5 строк

Место: `src/moonlight/vita_video_renderer.c:533/569`,
vita_video_release_frame_buffers_internal/moonlight_video_release_frame_buffers.
Коммит **61e7094**. Прежнее тело получило уже существующее публичное имя; единственный
forwarder удалён. C linkage, callers, native calls, pending/generation и порядок
unmap/free/reset не менялись.

Риск: низкий. Проверки: references, 17 startup-runtime + 9 startup-interrupt +
2 video crop/scaling теста, diff check. На Vita: deferred release после stop,
failed startup и повторный запуск в обоих профилях; native GPU lifetime не доказан тестами.

### 12. [delete] Непрочитанное присваивание длины logger — −4 строки

Место: `src/common.cpp:38`, vita_debug_log. Коммит **d785e9f**.
После vsnprintf длина ограничивалась и больше не читалась. Удалён только dead
adjustment; `vsnprintf(line, sizeof(line), ...)`, negative-result guard,
Debug-log gate и вывод через `%s` сохранены.

Риск: низкий. Проверки: source dataflow/buffer-bound assertions и diff check,
позже общий 184-test gate. Нового mirror-теста для dead assignment нет.
На Vita: обычный/усечённый console log и Off; native libc/console и сборка обоих
профилей — проверка сопровождающего.

## Требуют дополнительных доказательств; не применены

### [stdlib] Два AES-ECB helpers — потенциально около −25 строк

Текущее место: `src/backend/legacy/legacy_gamestream.c:1353/1382`, encrypt_ecb и
decrypt_ecb. Общие block-length/OOM/padding/update/free ветки можно выразить одним
private crypt_ecb с режимом через имеющиеся EVP_CipherInit_ex/EVP_CipherUpdate.
Эти функции объявлены в установленном OpenSSL evp.h:643/646; новой зависимости не нужно.

Эскиз: `crypt_ecb(input, length, key, output, encrypt)` с прежними проверками и
кодами INVALID/OOM/INTERNAL; внутри `EVP_CipherInit_ex(..., encrypt)` и
`EVP_CipherUpdate(...)`. Никакие проверки успеха или free не выкидываются.

Риск: средний. Пока подтверждено дублирование, **не подтверждена** эквивалентность
на реальной Vita OpenSSL. Нужны native known-answer vectors обоих направлений,
ошибки каждой стадии/cleanup и полный PIN success/wrong-PIN/cancel/reconnect.
Текущие mocks отдельных Encrypt/Decrypt API этого не доказывают. Поэтому не применено.

### [yagni] Dormant SPS fixup flags — потенциально около −14 строк

Текущее место: `src/moonlight/sps.h:22/24/28`, `sps.c:40/58/63`, gs_sps_fix;
единственный caller `src/moonlight/vita_video_renderer.c:641/643` передаёт
GS_SPS_BITSTREAM_FIXUP. Убрать private flags parameter/unused VST/CLI branches,
оставив обязательный BITSTREAM block, можно после фиксации codec policy.

Эскиз: `gs_sps_fix(sps, out_buf, out_offset)`; level/ref-frame/bitstream writes
остаются, VST/CLI поля сохраняются как сейчас. Риск: средний, это портированная
codec compatibility логика. Нужны byte-for-byte NAL fixtures до/после для Annex-B
prefixes, VUI states и разрешений, затем сборка обоих профилей и Vita streaming.
Одного отсутствия других callers для такого изменения недостаточно; не применено.

## Отклонённые идеи — к потенциальной экономии не прибавлены

- `src/moonlight/api.cpp:411/417` всегда передаёт unsupported=true в legacy init.
  Удалять version admission/error checks ради этого нельзя: это решение о совместимости,
  а не подтверждённое удаление лишнего кода. Проверки оставлены.
- Native AppSettings callbacks с пустым телом входят в ABI callback table.
  Заменять их NULL без проверенного native контракта нельзя.
- Service/backend interfaces с одним backend, event FIFO/snapshots, owner-worker,
  cancellation, frame-generation guards и retained buffers имеют разные контракты.
  Их наличие не признано лишней абстракцией.
- Похожие runtime disconnect controllers проверяют разные checkpoints, ожидание
  native OK и состояние framebuffer pool. Новый общий state-machine/framework
  создал бы связанность ради сокращения тестов; не предложен.
- В Python есть небольшой повтор setup/assertions, но нет single-reference helper
  без объяснения unittest/import/dynamic registration. Обобщение интерпретаторов
  и extractor regex не даёт подтверждённого полезного выигрыша.
- У SceRtc/SceHttp/SceSsl stubs нет прямых собственных callers, но это не доказывает
  отсутствие transitive/native imports. Без link/import проверки не удалялись.
  CMake/profile branches и dependency source lists не уплотнялись косметически.
- PNG authoring уже использует stdlib. Wchar-conversion не заменялась функцией
  с неподтверждённым Vita ABI. Новая библиотека или custom protocol не предложены.

Вне основного списка: gs_sps_fix не проверяет int-результаты read_nal_unit/
write_nal_unit (`sps.c:43/89`, declarations в h264_stream.h:385/412). Это отдельная
проверка корректности, а не экономия LOC; в этом audit-проходе не изменялось.

## Покрытие и пробелы

- Прочитаны обязательные AGENTS/skill/README/ROADMAP/tests README/UI. Исходники
  проверялись заново; история/старый отчёт служили только контекстом.
- Обойдены все 54 файла src и 36 include: pages/common/OptionMenu,
  app/bootstrap/event/heap/budget, services/adapters/legacy store/discovery/GameStream,
  API/settings/SPS/stats/stream, video/audio/input/touch/motion/system/transport.
  Проверены объявления, callbacks и ветви normal/system/test/heap-trace профилей.
- Обойдены 3 CXML/settings/locale XML и ссылки на 5 текстур; LiveArea template/asset
  manifest также проверены. Рабочие template/style/checkbox/native layouts сохранены;
  PNG-пиксели и внешний вид заново не оценивались.
- Python tests/tools: AST/definitions/import/reference/duplicate survey всех файлов
  (29 исходных тестовых Python-файлов, новый OptionMenu и generator), ручная проверка
  затронутых helpers/assertions, общий запуск. Native fixture и все 5 runtime
  controllers проверены как исходники/условные включения; не компилировались/не запускались.
  Это не независимая построчная сертификация каждого mock/interpreter.
- Скрипты/config: оба shell script, CMake, exports, gitignore/gitmodules,
  workspace config, generator и compatibility headers. `bash -n` прошёл.
  third_party — только использование/headers/dependency integration, не полный
  аудит и не переписывание vendored implementation. build/generated исключены.
- Пробелы: compiler/type/link/ABI, реальная синхронизация, native UI, curl/OpenSSL,
  filesystem failures/durability, decoder/GPU и Vita runtime. Дополнительные
  acceptance/release gates не закрывались предыдущими либо mock результатами.

## Первый проход и общий потенциал

Три наиболее полезных независимых изменения для первого прохода уже выполнены:
OptionMenu **1d337b6** (−31), dead page APIs **3858f0a** (−31), mDNS orphan path
**6e6b3cc** (−19). Остальные подтверждённые небольшие изменения также сохранены
отдельно, а не оставлены на следующее подтверждение.

**Факт: −140 production строк. Остаточный потенциал: около −39 строк / 0 зависимостей.**
Условный общий объём этого нового прохода — около 179 строк с учётом уже выполненных
140, только если обе отложенные идеи подтвердятся. Это не дополнительная экономия
поверх 140 и не включает предыдущие проходы. После них других обоснованных крупных
сокращений в рассмотренном scope не подтверждено; следующие изменения требуют
новых доказательств, а сборка и Vita-проверки остаются за сопровождающим.
