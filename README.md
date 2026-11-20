# TuneForge — اوتو تيون (VST3) لويندوز

بلجن تصحيح نغمة من تأليفي من الصفر (كود أصلي بالكامل، مش نسخة من أي برنامج تاني).
بيشتغل كـ VST3 في أي DAW على ويندوز (FL Studio, Reaper, Ableton, Cubase, Studio One...)، وفيه نسخة Standalone تجرب بيها بالمايك على طول.

## الإمكانيات

| الميزة | الشرح |
|---|---|
| **Retune Speed** | 0 ms = صوت الاوتو تيون الروبوتي المعروف (T-Pain)، وكل ما تزود يبقى طبيعي أكتر (لحد 400 ms) |
| **Humanize** | بيبطّأ التصحيح على النوتات الطويلة عشان تحافظ على طبيعيتها |
| **Flex Tune** | منطقة حوالين النوتة (الخطوط الصفرا في العداد) الصوت جواها ميتصححش |
| **Modern / Classic** | Classic = تصحيح خطي مباشر من غير Humanize/Flex |
| **Vocal Range** | Soprano / Alto-Tenor / Low Male / Instrument (بيأثر على الدقة والـ latency) |
| **Key & Scale** | Chromatic, Major, Minor, Pentatonic, أو Custom |
| **Edit Notes** | دوس على أي نوتة في الكيبورد تفعلها أو تلغيها كهدف للتصحيح |
| **MIDI Perform** | النوتات اللي بتضغط عليها من كيبورد MIDI بتبقى هي الأهداف الوحيدة |
| **Formant Designer** | Transpose (±12 نغمة) و Formant (±7 نغمات) — الـ formant بيتحافظ عليه تلقائي |
| **Vibrato** | Preserve بيحافظ على الفيبراتو، و Cut/Boost بيقلله أو يزوده |
| **Latency Mode** | Live (أقل تأخير) / Studio (دقة أعلى، والـ DAW بيعوّض التأخير) |
| **Reference Tuning** | معايرة A4 (415–466 Hz) |

التقنية: كشف النغمة بخوارزمية YIN مع تحسين بدقة عالية، وإعادة التركيب بـ TD-PSOLA (بتحافظ على لون الصوت). الأصوات غير المنغمة (s, t, نفَس) بتعدي من غير تعديل.

التأخير (latency) بيتبلّغ للـ DAW تلقائي: حوالي 35 ms لـ Alto/Tenor و 20 ms لـ Soprano.

## إزاي تاخد نسخة ويندوز (من غير ما تثبّت أي حاجة على جهازك)

1. اعمل حساب مجاني على [github.com](https://github.com).
2. اعمل Repository جديد (زرار **New**)، وسمّيه `TuneForge`.
3. ارفع كل محتويات الفولدر ده فيه (**Add file → Upload files**). تأكد إن فولدر `.github` اترفع معاه (لو مش باين عندك على ويندوز، فعّل "إظهار الملفات المخفية").
4. افتح تبويب **Actions** → اختار **Build TuneForge (Windows)** → **Run workflow**.
5. استنى 10–15 دقيقة (أول مرة بتنزّل مكتبة JUCE). بعد ما يخلص افتح آخر تشغيلة ونزّل الملف **TuneForge-Windows** من قسم Artifacts.
6. فك الضغط، هتلاقي:
   - `TuneForge.vst3` ← انسخه (هو فولدر) إلى `C:\Program Files\Common Files\VST3`
   - `TuneForge-Standalone.exe` ← شغّله على طول لو عايز تجرب بالمايك.
7. افتح الـ DAW واعمل **Rescan plugins**.

## لو عندك Visual Studio 2022 وعايز تبني بنفسك

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target TuneForge_VST3
```
الناتج في `build/TuneForge_artefacts/Release/VST3/`.

## إعدادات مقترحة

- **صوت T-Pain / Trap**: Retune Speed = 0، Humanize = 0، Flex Tune = 0، Key & Scale على مقام الأغنية.
- **تصحيح طبيعي ناعم**: Retune Speed = 30–80، Humanize = 50، Flex Tune = 30–50.
- **ماسك الفيبراتو**: فعّل Preserve، وCut/Boost على 0 (أو قلل لو الفيبراتو مبالغ فيه).

## نصائح استخدام

- حط البلجن على تراك صوت واحد (مونو) وحيد، من غير ريفرب أو ديلاي قبله.
- اختار **Key & Scale** صح، ده أكبر تأثير على جودة النتيجة.
- لو الصوت بيطلع بنغمة غلط (octave) غيّر **Vocal Range** لمدى صوتك.

## اختبار المحرك

محرك التصحيح (`Source/PitchCorrector.h`) ليه اختبارات بتتشغل من غير JUCE:

```
cd tests
g++ -O2 -std=c++17 -I../Source test_engine.cpp -o test_engine && ./test_engine
```
بتتأكد إن النغمة بتتصحح لدقة أقل من 1 سنت تقريباً، وإن الـ scale والـ transpose والـ formant والفيبراتو والصمت شغالين صح.
