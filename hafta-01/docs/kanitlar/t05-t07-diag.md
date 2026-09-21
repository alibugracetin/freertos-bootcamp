# T-05 · T-06 · T-07 kanıtları

**Tarih:** 22.09.2026 · **Kart:** STM32F4DISCOVERY · **Okuma yöntemi:** `tools/read_diag.py` (SWD, işlemci durdurulmadan; UART trafiği yok)

## T-05 · Kayıt defteri öz-testi

Açılışta, scheduler başlamadan `rec_selfcheck()` koşar.

| Alan | Değer | Beklenen |
|---|---|---|
| `selfcheck` | 1 | 1 (geçti) |
| 100 sahte olaydan açılan | 64 | 64 (havuz boyutu) |
| Taşma nedeniyle reddedilen | 36 | 36 |

Sınanan kurallar: havuz doluyken yeni kayıt reddedilir; başka kimliğe ait slota damga yazılamaz; kayıt iki kez kapatılamaz; kapanmış ama dökülmemiş kaydın üzerine yazılamaz; `have` bitleri doğru tutulur.

## T-07 · Görev sayısı ve defaultTask hatası

**İlk okuma:** `task_count = 5` (beklenen 4 = 3 uygulama + Idle).

**Kök neden:** CubeMX'in öncelik 24'teki `defaultTask`'ı scheduler başlamadan dışarıdan silinmişti. FreeRTOS scheduler öncesinde `pxCurrentTCB`'yi en yüksek öncelikli göreve ayarlar; `vTaskDelete()` bu yüzden onu "kendini silen görev" sanıp yalnızca bekleme listesine koydu. Uygulama görevleri (öncelik 1–3) `pxCurrentTCB`'yi değiştirmediği için scheduler **silinmiş görevi başlattı**: `osDelay(1)` ile her 1 ms'de diğer görevleri kesen bir zombi.

**Düzeltme:** `defaultTask` ilk koşusunda `vTaskDelete(NULL)` ile kendini siler (`freertos.c`, USER CODE StartDefaultTask).

**Sonrası:** `task_count = 4`. `heap_free − heap_min_free = 11 160 − 10 248 = 912 bayt` — Idle görevi silinen görevin TCB ve stack'ini geri vermiş.

## T-06 · Bırakış zıplaması (FR-12b)

### Test 1 — yalnızca 30 ms filtresi (şartnamedeki yaklaşım)

Kullanıcı 5 kez bastı: normal, normal, uzun, normal, uzun. **Kabul edilen: 8.**

Kayıt defterinden okunan t₀ zaman çizelgesi:

| id | önceki olaydan sonra [ms] | yorum |
|---|---|---|
| 1 | — | 1. basış |
| 2 | **176,1** | bırakış zıplaması (1. basış) |
| 3 | 1 862,5 | 2. basış |
| 4 | 2 639,2 | 3. basış (uzun) |
| 5 | **1 318,6** | bırakış zıplaması (3. basış) |
| 6 | 1 584,7 | 4. basış |
| 7 | 1 895,9 | 5. basış (uzun) |
| 8 | **1 239,6** | bırakış zıplaması (5. basış) |

30 ms penceresi yalnızca basış anını korur; bırakış yüzlerce ms sonra gelir ve pencerenin dışında kalır.

### Test 2 — silahlı filtre (FR-12b)

Aynı basış dizisi. **Kabul edilen: 5.** `unarmed_rej = 1`, `debounce_rej = 0`.

### Her iki testte ISR → görev süresi

| | t₁ − t₀ son | t₁ − t₀ en fazla |
|---|---|---|
| Test 1 | 11 µs | 13 µs |
| Test 2 | 11 µs | 14 µs |

Telemetri kapalı (S0 koşulu), yük yok. Bu, ISR'ın kalanı + bağlam geçişi + ButtonTask'ın uyanma süresidir.

## Diğer

- `fault = 0`, kuyruk ve kayıt kayıpları 0.
- ButtonTask stack'i: 512 word'ün 470'i hiç kullanılmamış.
