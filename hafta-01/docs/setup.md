# Sıfırdan kurulum

Depoyu klonlamış birinin firmware'i derleyip karta yükleyebilmesi ve arayüzü çalıştırabilmesi için gereken her şey.

## 1. Gereken araçlar

| Araç | Sürüm (bu projede kullanılan) | Not |
|---|---|---|
| STM32CubeIDE | 2.2.0 | CMake, Ninja, arm-none-eabi-gcc 14.3 ve STM32_Programmer_CLI bu kurulumdan gelir |
| STM32CubeMX | 6.18.1 | Yalnızca `.ioc` değişecekse gerekir |
| STM32Cube FW_F4 | V1.28.3 | CubeMX tarafından indirilir |
| Python | 3.12 | Arayüz ve analiz betikleri |
| Doxygen | 1.18 | Kod dokümantasyonunu yeniden üretmek için |
| Graphviz | 16.1 | Doxygen çağrı grafikleri (opsiyonel) |
| Git | 2.55 | |

Windows'ta:

```bash
winget install --id Git.Git -e
winget install --id Python.Python.3.12 -e
winget install --id DimitriVanHeesch.Doxygen -e
winget install --id Graphviz.Graphviz -e
```

## 2. Donanım

1. FT232RL dönüştürücünün **VCCIO jumper'ını 3,3 V**'a alın.
2. Kabloları bağlayın:

   | FT232RL | Discovery |
   |---|---|
   | TXD | PA3 |
   | RXD | PA2 |
   | GND | GND |
   | VCC | *bağlanmaz* |

3. Discovery'yi **mini-USB** (ST-LINK) ile, FT232RL'i kendi USB'siyle PC'ye takın.
4. Aygıt yöneticisinden FT232'nin COM numarasını not edin (bu projede COM7).

## 3. Firmware

```bash
PL="C:/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins"
export PATH="$PL/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin:$PL/com.st.stm32cube.ide.mcu.externaltools.ninja.win32_1.1.200.202606260906/tools/bin:$PL/com.st.stm32cube.ide.mcu.externaltools.cmake.win32_1.1.200.202605190741/tools/bin:$PATH"

cd hafta-01/firmware
cmake --preset Debug -DHW_SELFTEST=OFF
cmake --build --preset Debug
STM32_Programmer_CLI -c port=SWD -w build/Debug/firmware.elf -v -rst
```

Beklenen: `0 hata, 0 uyarı`, `Download verified successfully`.

> **Not.** Proje CubeIDE'nin managed build'i yerine **CMake** formatındadır (gerekçe: README §9). CubeIDE 2.2 CMake projelerini kendi araçlarıyla derler; yukarıdaki komutlar IDE açmadan da çalışır.

## 4. Kurulumu doğrulama

Donanımın doğru kurulduğunu ölçüme başlamadan sınayın. Öz-test firmware'i RTOS'u başlatmaz; UART, LED, buton ve TIM2'yi kontrol eder:

```bash
cmake --preset Debug -DHW_SELFTEST=ON
cmake --build --preset Debug
STM32_Programmer_CLI -c port=SWD -w build/Debug/firmware.elf -v -rst
```

Seri portu 115200 8N1 ile açın. Beklenen çıktı:

```
HWTEST,BOOT,SYSCLK=168000000,HCLK=168000000,PCLK1=42000000,PCLK2=84000000
HWTEST,TIM,round=1,tim2_us=1000999,dwt_us=1000998,diff_us=1
HWTEST,WRAP,before=0xFFFFFFF0,after=0x00000010,delta=32,expected=32
HWTEST,INFO,btn_level_idle=0,press the blue button
```

- `diff_us` birkaç µs'den büyükse TIM2 ön-bölücüsü veya saat ağacı yanlıştır.
- Butona basınca `level=1` görünmelidir (aktif-HIGH).
- LED'ler turuncu → kırmızı → mavi sırasıyla dönmelidir.

Bitince **ölçüm firmware'ine geri dönün** (`-DHW_SELFTEST=OFF`); bu seçenek CMake önbelleğinde kalır.

Kart çalışırken tanı yapısını okumak için (UART'a trafik eklemez):

```bash
python hafta-01/tools/read_diag.py
```

Beklenen: `magic = 0xD1A60002`, `selfcheck = 1`, `task_count = 4`, `fault = 0`.

## 5. Arayüz

```bash
pip install -r hafta-01/interface/requirements.txt
python hafta-01/interface/main.py
```

Portu seçin, **Bağlan**. Kart durumu "Boşta" görünmelidir. Ölçüm prosedürü: [`olcum-protokolu.md`](olcum-protokolu.md).

## 6. Analizi yeniden üretme

Ham CSV'lerden tabloları ve grafikleri yeniden üretmek:

```bash
python hafta-01/analysis/analyze.py        # → analysis/ozet-tablo.md
python hafta-01/interface/make_plots.py    # → analysis/plots/*.png
```

Kod dokümantasyonu:

```bash
cd hafta-01/docs && doxygen Doxyfile       # → docs/doxygen/html/index.html
```

Beklenen: `doxygen-warnings.txt` **boş**.

## 7. `.ioc` değişirse

CubeMX betik modunda yeniden üretim:

```bash
cat > mx.txt <<'EOF'
config load <depo>/hafta-01/firmware/firmware.ioc
project toolchain CMake
project name firmware
project path <depo>/hafta-01
project generate
exit
EOF
STM32CubeMX.exe -q mx.txt
```

⚠️ CubeMX 6.18.1 betik modunda `syscalls.c` ve `sysmem.c` dosyalarını hatalı yola yazar; bu dosyalar depoda mevcuttur, silinmemelidir.

⚠️ Yeniden üretimden sonra USER CODE blokları korunur, ancak `configUSE_TIMERS` ve `defaultTask` düzeltmelerinin yerinde olduğunu doğrulayın (bkz. `code-notes.md` §9).
