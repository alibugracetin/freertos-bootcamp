# Protokol dışı koşu — 23.09.2026, 21:1x

Altı senaryonun tamamı 30'ar olayla ölçüldü ve veri teknik olarak sağlamdır
(kayıp yok, çerçeve hatası yok). Ancak **MR-02 ihlal edilmiştir**: basışlar
arasında en az 0,5 saniye olması gerekirken aralıkların çoğu bunun altında kaldı.

| Senaryo | min aralık | ortalama | 500 ms altı |
|---|---|---|---|
| S0 | 173 ms | 329 ms | 26 / 29 |
| S1 | 158 ms | 321 ms | 27 / 29 |
| S2 | 170 ms | 295 ms | 24 / 29 |
| S3 | 285 ms | 474 ms | 20 / 29 |
| S4 | 296 ms | 672 ms | 15 / 29 |
| S5 | 179 ms | 482 ms | 21 / 29 |

Hızlı basış, buton mesajlarının hat yükünü artırır ve özellikle S5'te kuyruğun
dolmasını hızlandırır. Bu nedenle resmî ölçüm, arayüze eklenen tempo
göstergesiyle (≥ 500 ms, rastgele aralık) yeniden alınmıştır.

Bu koşu raporda **ön deneme** olarak anılır; S5'teki aşırı yük rejiminin
basış hızından bağımsız olup olmadığını karşılaştırmak için saklanmıştır.
