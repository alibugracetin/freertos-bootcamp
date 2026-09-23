# Yapay zekâ kullanımı

Ödev, yapay zekâ kullanımına açıkça izin veriyor. Bu dosya kullanımın kapsamını, iş bölümünü ve üretilen çıktının nasıl doğrulandığını olduğu gibi anlatır.

**Kullanılan araç:** Claude (Anthropic), Claude Code üzerinden. Proje boyunca aynı oturum dizisinde çalışıldı.

---

## 1. Kapsam — dürüst tablo

| İş | Kim yaptı |
|---|---|
| Ödev şartnamesinin gereksinimlere çevrilmesi | AI üretti, ben okuyup onayladım |
| Tasarım kararları (görev yapısı, kuyruklar, zaman damgası noktaları) | AI önerdi ve gerekçelendirdi, ben onayladım |
| Firmware kodu (C) | **Büyük kısmı AI tarafından yazıldı** |
| CubeMX projesi | AI betik modunda üretti |
| PC arayüzü ve grafik kodu (Python) | **Büyük kısmı AI tarafından yazıldı** |
| Derleme, yükleme, SWD ile tanı okuma | AI komut satırından yaptı |
| **Donanım kurulumu ve kablolama** | **Ben** |
| **Tüm fiziksel ölçümler (180 basış + doğrulama koşuları)** | **Ben** |
| **LED, buton ve görsel doğrulamalar** | **Ben** |
| Yöntem kararları (senaryoların çalışma zamanı komutla değiştirilmesi, arayüzde grafik istenmesi) | **Ben** |
| Ölçüm raporu | AI yazdı, ben okudum |

Kısacası: **kodun büyük bölümü AI tarafından üretildi**, donanım tarafındaki her şey ve ölçümlerin tamamı bana ait. Bu dosyanın amacı bunu gizlemek değil, net göstermektir.

## 2. Nasıl çalışıldı — spec-driven development

Kod yazılmadan önce üç katmanlı bir belge zinciri kuruldu ve her katman bir sonrakine geçmeden önce benim onayımdan geçti:

1. `specs/01-requirements.md` — **ne** yapılacak (ödev şartnamesinden türetilmiş, kimliklendirilmiş ve doğrulanabilir maddeler)
2. `specs/02-design.md` — **nasıl** yapılacak (her kararın gerekçesiyle)
3. `specs/03-tasks.md` — hangi sırayla, her adımın "bitti kanıtı" ile

Bu yöntemin somut faydası: ölçüm mekanizmasını anlamadığım bir noktada soru sordum ("ölçümleri nasıl yapacağız?") ve tasarımda eksik bir bölüm olduğu **kod yazılmadan** ortaya çıktı. Kayıt defterinin yaşam döngüsü ve eşzamanlılık kuralları o soru üzerine tasarıma eklendi.

Belgeler depoda duruyor; süreç izlenebilir.

## 3. Üretilen kod nasıl kontrol edildi

Kodu satır satır okuyup doğrulayacak seviyede RTOS bilgim yoktu. Bunun yerine **her adımın ölçülebilir bir kanıtı** olmasını istedim ve kanıtları kendim gördüm:

| Adım | Kanıt |
|---|---|
| Zaman kaynağı doğru mu | Aynı 1 saniye iki bağımsız sayaçla ölçüldü (TIM2 ve çekirdeğin DWT sayacı); fark 1 µs |
| Görev sayısı doğru mu | Karttan okunan `task_count = 4` (3 uygulama + Idle) |
| Timer daemon kapandı mı | ELF sembol tablosunda `prvTimerTask` yok |
| Buton aktif seviyesi | Basılıyken 1, boştayken 0 — kendi testimle gördüm |
| Butonun zıpladığı | 6 basışta 9 kesme kenarı ölçüldü |
| t₄ gerçekten son bit mi | HAL kaynağı okundu **ve** DMA–TC farkı ölçüldü: 173 µs = tam 2 karakter |
| Hat süresi yükten bağımsız mı | Altı senaryoda `t₄−t₃` = 5 553–5 555 µs |
| CPU yükü kalibrasyonu | Ölçülen 2 001,6 µs ve 5 008,4 µs (hedef 2 000 / 5 000) |
| Ölçüm protokolü | Basış aralıkları: 0/29 ihlal, hepsi ≥ 500 ms |

Ayrıca LED'leri, arayüzü ve buton tepkisini gözle doğruladım; ölçüm sırasında sistemin yavaşladığını doğrudan gördüm (S5'te "Butona basıldı" yazısı basıştan gözle görülür biçimde sonra beliriyordu).

## 4. AI'ın önerilerini değiştirdiğim yerler

- **Senaryo seçimi.** AI derleme zamanı `#define` önerdi (daha basit). Ben senaryoların **arayüzden, çalışırken** değiştirilmesini istedim. Bu, komut protokolü ve deney durum makinesi gerektirdi; AI görev sayısını 3'te tutacak şekilde çözdü.
- **Grafikler.** Plana göre grafikler sona bırakılmıştı. Arayüzde grafik görmek istediğimi söyledim, öne alındı.
- **Doxygen.** Kod dokümantasyonunun Doxygen ile üretilmesini ben istedim.
- **Ölçüm sırası.** Senaryoları planlanandan farklı sırayla koştum.

## 5. AI'ın hatalarını ölçüm yakaladı

Üretilen kod ilk seferde doğru değildi; hatalar ölçümle ortaya çıktı ve düzeltildi. Rapora ve kod notlarına yazıldılar:

- **Zombi görev.** CubeMX'in ürettiği fazladan görev yanlış anda silinmişti; ölmemiş, her 1 ms'de diğer görevleri kesiyordu. `task_count = 5` okunarak yakalandı.
- **Bırakış zıplaması.** Şartnamedeki 30 ms filtresi yeterli sanılmıştı; 5 basışta 8 olay kaydedildi. Filtreye ikinci koşul eklendi.
- **Ölçüm aletinin kendi izi.** Tanı kodu, ölçülen aşamalardan birine (`t₃−t₂`) 125 µs ekliyordu. Bakım turu ölçüm yolundan çıkarıldı.
- **Flash'a gitmeyen sabit.** Derleme "0 hata" dedi ama yeni kalibrasyon değeri karta yazılmamıştı. Flash'tan okuyarak doğrulandı.
- **Kuyruk sayacının şişmesi.** Raporlanan kuyruk doluluğu, ölçüm fazını değil kayıt dökümünü yansıtıyordu.

## 6. Ne öğrendim

Bu ödevde benim için yeni olan şeyler: görev öncelikleri ve preemption'ın gecikmeye nasıl yansıdığı; kesme bağlamı ile görev bağlamı arasındaki veri paylaşımının neden kural gerektirdiği; bir zaman damgasının **nerede** alındığının ölçtüğü şeyi belirlediği; ve ölçüm aletinin ölçümü bozabileceği.

En çarpıcısı S5 sonucuydu: sistem hattın yalnızca %56'sı doluyken kilitlendi. Darboğaz bant genişliği değil, en düşük öncelikli görevin CPU'ya erişemmesiydi. Bunu ben tahmin edemezdim; ölçüm gösterdi ve mekanizmayı birlikte çözdük.

## 7. Sınır

Bu ödevin kodunu sıfırdan tek başıma yazamazdım. Yapabildiğim ve yaptığım şey: neyin ölçüleceğine karar vermek, kurulumu kurmak, ölçümleri almak, sonuçların tutarlı olup olmadığını sorgulamak ve her iddianın bir kanıtı olmasını istemek. Rapordaki sayıların tamamı kendi kartımdan, kendi basışlarımdan geldi; sentetik veri yok.
