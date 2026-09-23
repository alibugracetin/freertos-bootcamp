# Ölçüm protokolü

Altı senaryonun tamamı **aynı oturumda, aynı firmware ile** ölçülür (MR-07).
Bir senaryo yarım kalırsa o senaryo baştan koşulur; diğerleri etkilenmez.

## Ölçüm öncesi (bir kez)

| Kontrol | Beklenen |
|---|---|
| Firmware sürümü | Depodaki son commit ile derlenmiş, `k_scen` flash'tan doğrulanmış |
| Derleme | CMake **Debug**, `-O0`, `HW_SELFTEST=OFF` |
| CPU işi kalibrasyonu | S4 ≈ 2 000 µs, S5 ≈ 5 000 µs (T-16) |
| Kablolama | FT232RL TXD→PA3, RXD→PA2, GND→GND, VCC bağlı **değil** |
| Arayüz | `python hafta-01/interface/main.py`, COM7'ye bağlı |
| Hedef olay | **30** (MR-01) |

## Her senaryo için sıra

1. Sol panelden senaryoyu seç, **Hedef olay = 30**.
2. **▶ Başlat**. Arayüz `CMD,SCEN` ve `CMD,START,30` gönderir; kart sayaçları ve kayıt havuzunu sıfırlar (FR-86).
3. **Isınma 5 saniye** — LED turuncu. Bu sırada basma; basarsan `idle_press` sayılır, kayda girmez.
4. LED **mavi** ve durum "ÖLÇÜM" olunca **30 kez bas**:
   - basışlar arası **en az 0,5 saniye** (MR-02)
   - **düzenli ritim tutma** — aralıkları bilinçli olarak değiştir (bkz. aşağıdaki not)
   - her basışta "Butona basıldı · Olay N" görünmeli; sayaç `N / 30` ilerlemeli
5. 30. olaydan sonra LED **kırmızı** olur; kart telemetriyi durdurur, kuyruğu boşaltır, kayıtları döker.
6. Arayüz CSV'yi **kendiliğinden** yazar: `measurements/Sx.csv`, `Sx_diag.csv` ve `summary.csv`.
7. Günlükte uyarı var mı bak (eksik kayıt, çerçeve hatası). Varsa senaryoyu tekrarla.

**Sıra:** S0 → S1 → S2 → S3 → S4 → S5.

## Neden düzensiz basmak gerekiyor?

Telemetri sabit periyotla (10/20/100 ms) çalışır. Hep aynı ritimle basarsan
basışların telemetri periyodunun hep aynı fazına denk gelir ve `t₃ − t₂`
dağılımının yalnızca bir dilimini ölçersin. Bu, ölçümü olduğundan iyi ya da
kötü gösterir. Aralıkları değiştirmek (bazen 0,6 s, bazen 2 s, bazen 4 s)
fazın rastgele dağılmasını sağlar.

## Ölçüm sonrası

- `measurements/` altındaki altı CSV, altı `_diag.csv` ve `summary.csv` commit edilir.
- Grafikler sekmesinden **PNG kaydet** → `analysis/plots/`.
- Kayıp/hata sayaçları raporda **gizlenmeden** verilir (MR-09).

## Bir senaryo bozulursa

`STOP` → senaryoyu yeniden seç → `START`. Eski CSV üzerine yazılmaz;
`measurements/arsiv/` altına zaman damgasıyla taşınır.
