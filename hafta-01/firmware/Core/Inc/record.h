/**
 * @file    record.h
 * @brief   Ölçüm kayıt defteri: her buton olayının t₀…t₄ damgaları.
 * @ingroup record
 */

/**
 * @defgroup record Ölçüm Kayıt Defteri
 * @brief    Beş farklı bağlamın parça parça doldurduğu olay kayıtları.
 *
 * Bir olayın damgaları beş ayrı yerde alınır: t₀ buton ISR'ında, t₁ ve t₂
 * `ButtonTask`'ta, t₃ `UartTxTask`'ta, t₄ UART TC kesmesinde. Hiçbiri diğerini
 * beklemez; her biri olayın kimliğiyle ilgili kaydı bulup kendi alanını yazar.
 * Kayıtlar deney bittikten sonra toplu olarak dışa aktarılır, çünkü t₄ yanıt
 * gönderildikten *sonra* oluşur ve yanıtın içine konamaz (FR-67).
 *
 * **Eşzamanlılık (tasarım §3.1, FR-62):** Aynı kaydın alanları kesin bir sırayla
 * yazılır (t₀ → t₁,t₂ → t₃ → t₄); bir aşama ancak öncekinin sonucu kuyruktan
 * alındıktan sonra çalışabilir. Bu yüzden aynı kayda iki bağlam aynı anda
 * yazmaz ve kilit gerekmez. Farklı olaylar farklı slotlara düşer.
 *
 * **Slot kuralı:** Olay kimliği `id`, `id % APP_REC_POOL_SIZE` slotuna düşer.
 * Slot yalnızca boşken (`REC_FREE`) açılabilir; eski bir kaydın üzerine asla
 * yazılmaz (FR-61). Her yazma, slotun gerçekten o kimliğe ait olduğunu doğrular;
 * geç gelen bir damga başka bir olayın kaydını bozamaz (FR-46).
 */

#ifndef RECORD_H
#define RECORD_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   Bir kaydın yaşam döngüsündeki durumu.
 * @ingroup record
 */
typedef enum {
    REC_FREE = 0,    /**< Slot boş; açılabilir. Sıfırlanmış havuzun varsayılanı. */
    REC_OPEN,        /**< Olay sistemde ilerliyor; damgalar dolduruluyor. */
    REC_OK,          /**< t₄ alındı; ölçüm tamam. */
    REC_BTNQ_DROP,   /**< `buttonQ` doluydu; olay göreve ulaşamadı. */
    REC_TXQ_DROP,    /**< `txQ` doluydu; yanıt gönderilemedi (FR-34). */
    REC_TX_ERROR,    /**< UART gönderimi başlatılamadı (FR-44). */
    REC_TIMEOUT      /**< Gözetim süresi (1 s) içinde tamamlanmadı (MR-06). */
} RecStatus;

/**
 * @brief   Zaman damgası indeksi.
 * @ingroup record
 */
typedef enum {
    TS_T0 = 0,   /**< Buton ISR girişi (FR-11). */
    TS_T1,       /**< `ButtonTask` olayı aldıktan hemen sonra (FR-31). */
    TS_T2,       /**< Yanıt `xQueueSend`'den hemen önce (FR-32). */
    TS_T3,       /**< UART başlatma çağrısından hemen önce (FR-42). */
    TS_T4,       /**< UART TC olayı işlenirken (FR-43). */
    TS_COUNT     /**< Damga sayısı. */
} TsIndex;

/**
 * @brief   Tek bir buton olayının ölçüm kaydı.
 * @ingroup record
 */
typedef struct {
    uint32_t event_id;       /**< Kaydın sahibi olan olay kimliği. */
    uint32_t t[TS_COUNT];    /**< Damgalar [µs]; yalnızca `have` bitleri set olanlar geçerlidir. */
    uint8_t  have;           /**< Bit i set ⇒ t[i] alındı. CSV'de boş alan ile 0'ı ayırır (MR-21). */
    uint8_t  status;         /**< @ref RecStatus değeri. */
} EventRecord;

/**
 * @brief   Yeni bir olay için kayıt açar ve t₀'ı yazar.
 * @ingroup record
 * @param   id      Olay kimliği (> 0).
 * @param   t0_us   ISR girişinde alınan zaman [µs].
 * @return  `true`: kayıt açıldı. `false`: slot boş değil (taşma, FR-61) veya `id` 0.
 *
 * @note    **Yalnızca buton ISR'ından** çağrılır.
 * @par Zaman damgası
 *      **t₀** kaydedilir.
 */
bool rec_open(uint32_t id, uint32_t t0_us);

/**
 * @brief   Açık bir kayda bir zaman damgası yazar.
 * @ingroup record
 * @param   id      Olay kimliği.
 * @param   idx     Damga indeksi (@ref TS_T1 … @ref TS_T4).
 * @param   t_us    Zaman [µs].
 * @return  `true`: yazıldı. `false`: slot bu kimliğe ait değil veya kayıt açık değil.
 *
 * @note    Görev ve ISR bağlamında çağrılabilir. Başarısızlık çağıranın
 *          kendi bağlamındaki sayaçta sayılmalıdır (tasarım §3.1).
 */
bool rec_stamp(uint32_t id, TsIndex idx, uint32_t t_us);

/**
 * @brief   Açık bir kaydı verilen durumla kapatır.
 * @ingroup record
 * @param   id      Olay kimliği.
 * @param   status  Son durum (`REC_OK` veya bir hata durumu).
 * @return  `true`: kapatıldı. `false`: slot bu kimliğe ait değil veya kayıt zaten kapalı.
 *
 * @note    Görev ve ISR bağlamında çağrılabilir.
 */
bool rec_close(uint32_t id, RecStatus status);

/**
 * @brief   Bir slotun anlık kopyasını döndürür (dışa aktarım için).
 * @ingroup record
 * @param   slot    Slot indeksi, `0 … APP_REC_POOL_SIZE-1`.
 * @param   out     Kopyanın yazılacağı yer.
 * @return  `false`: slot indeksi geçersiz.
 *
 * @note    Yalnızca ölçüm durmuşken (hiçbir ISR/görev kayda yazmıyorken) çağrılmalıdır.
 */
bool rec_read(uint32_t slot, EventRecord *out);

/**
 * @brief   Hâlâ açık (`REC_OPEN`) kayıt sayısı.
 * @ingroup record
 * @return  Açık kayıt sayısı. DRAINING durumunda ölçümün bitip bitmediğini anlamak için.
 */
uint32_t rec_count_open(void);

/**
 * @brief   Açık kalan tüm kayıtları `REC_TIMEOUT` ile kapatır (MR-06).
 * @ingroup record
 * @return  Kapatılan kayıt sayısı.
 * @note    Yalnızca yeni olay açılmıyorken (ölçüm durmuşken) çağrılmalıdır.
 */
uint32_t rec_timeout_open(void);

/**
 * @brief   Tüm slotları `REC_FREE` yapar.
 * @ingroup record
 * @note    Yalnızca ölçüm durmuşken çağrılmalıdır (açılış veya senaryo değişimi, FR-86).
 */
void rec_reset(void);

/**
 * @brief   Kayıt defterinin kurallarını karta özgü koşullarda sınar (açılış öz-testi).
 * @ingroup record
 * @param   opened    Çıktı: 100 sahte olaydan açılabilen kayıt sayısı (beklenen: havuz boyutu).
 * @param   rejected  Çıktı: taşma nedeniyle reddedilen sayı (beklenen: 100 − havuz boyutu).
 * @return  `true`: tüm kontroller geçti.
 *
 * Sınanan kurallar: havuz dolunca yeni kayıt reddedilir; başka kimliğe ait slota
 * damga yazılamaz; kayıt iki kez kapatılamaz; kapanmış ama dökülmemiş kaydın
 * üzerine yazılamaz; damga ve `have` bitleri doğru tutulur. Çıkışta havuz sıfırlanır.
 *
 * @note    Scheduler başlamadan önce çağrılmalıdır (T-05 kanıtı).
 */
bool rec_selfcheck(uint32_t *opened, uint32_t *rejected);

#endif /* RECORD_H */
