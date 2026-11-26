#include "ogplay/app/ui_text.h"
#include "ogplay/app/ui_message.h"
#include "ogplay/hal/host_environment.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace ogplay::app {
namespace {
constexpr std::size_t kLanguageCount = kUiLanguages.size();

struct Entry {
    UiText id;
    // Columns follow kUiLanguages: zh-CN, en-US, zh-TW, ja-JP, ko-KR, ru-RU, es-ES, pt-BR, fr-FR.
    std::array<std::string_view, kLanguageCount> text;
};

constexpr std::array<std::string_view, kLanguageCount> kTags{"zh-CN", "en-US", "zh-TW", "ja-JP", "ko-KR",
                                                             "ru-RU", "es-ES", "pt-BR", "fr-FR"};

// clang-format off: keep every translation of a row together.
constexpr auto kText = std::to_array<Entry>({
    {UiText::close_game_title, {"关闭游戏", "Close Game", "關閉遊戲", "ゲームを終了", "게임 종료", "Закрыть игру", "Cerrar juego", "Fechar jogo", "Fermer le jeu"}},
    {UiText::close_game_message, {"确定要关闭游戏吗？未保存的进度可能会丢失。", "Close the game? Unsaved progress may be lost.", "確定要關閉遊戲嗎？未儲存的進度可能會遺失。", "ゲームを終了しますか？保存していない進行状況は失われる可能性があります。", "게임을 종료할까요? 저장하지 않은 진행 상황이 사라질 수 있습니다.", "Закрыть игру? Несохранённый прогресс может быть потерян.", "¿Cerrar el juego? Puede perderse el progreso no guardado.", "Fechar o jogo? O progresso não salvo pode ser perdido.", "Fermer le jeu ? La progression non sauvegardée risque d’être perdue."}},
    {UiText::close_game_confirm, {"关闭", "Close", "關閉", "終了", "종료", "Закрыть", "Cerrar", "Fechar", "Fermer"}},
    {UiText::close_game_cancel, {"取消", "Cancel", "取消", "キャンセル", "취소", "Отмена", "Cancelar", "Cancelar", "Annuler"}},
    {UiText::close_game_library, {"回到游戏库", "Game Library", "回到遊戲庫", "ライブラリへ戻る", "라이브러리로", "В библиотеку", "Ir a la biblioteca", "Ir para a biblioteca", "Bibliothèque"}},
    {UiText::close_game_library_message, {"关闭将结束游戏，未保存的进度可能会丢失；回到游戏库，游戏会在后台继续运行。", "Closing ends the game and unsaved progress may be lost. Game Library keeps it running in the background.", "關閉將結束遊戲，未儲存的進度可能會遺失；回到遊戲庫，遊戲會在背景繼續執行。", "終了するとゲームが終わり、保存していない進行状況は失われる可能性があります。ライブラリへ戻ると、ゲームはバックグラウンドで動き続けます。", "종료하면 게임이 끝나고 저장하지 않은 진행 상황이 사라질 수 있습니다. 라이브러리로 돌아가면 게임은 백그라운드에서 계속 실행됩니다.", "Закрытие завершит игру, несохранённый прогресс может быть потерян. В библиотеке игра продолжит работать в фоне.", "Al cerrar, el juego termina y puede perderse el progreso no guardado. En la biblioteca, el juego sigue ejecutándose en segundo plano.", "Fechar encerra o jogo e o progresso não salvo pode ser perdido. Na biblioteca, o jogo continua rodando em segundo plano.", "Fermer met fin au jeu et la progression non sauvegardée risque d’être perdue. Dans la bibliothèque, le jeu continue en arrière-plan."}},
    {UiText::menu_hide_app, {"隐藏 OGPlay", "Hide OGPlay", "隱藏 OGPlay", "OGPlay を隠す", "OGPlay 가리기", "Скрыть OGPlay", "Ocultar OGPlay", "Ocultar OGPlay", "Masquer OGPlay"}},
    {UiText::menu_hide_others, {"隐藏其他", "Hide Others", "隱藏其他", "ほかを隠す", "기타 가리기", "Скрыть остальные", "Ocultar otros", "Ocultar outros", "Masquer les autres"}},
    {UiText::menu_show_all, {"全部显示", "Show All", "全部顯示", "すべてを表示", "모두 보기", "Показать все", "Mostrar todo", "Mostrar tudo", "Tout afficher"}},
    {UiText::menu_quit, {"退出 OGPlay", "Quit OGPlay", "結束 OGPlay", "OGPlay を終了", "OGPlay 종료", "Завершить OGPlay", "Salir de OGPlay", "Encerrar OGPlay", "Quitter OGPlay"}},
    {UiText::menu_edit, {"编辑", "Edit", "編輯", "編集", "편집", "Правка", "Edición", "Editar", "Édition"}},
    {UiText::menu_undo, {"撤销", "Undo", "還原", "取り消す", "실행 취소", "Отменить", "Deshacer", "Desfazer", "Annuler"}},
    {UiText::menu_redo, {"重做", "Redo", "重做", "やり直す", "실행 복귀", "Повторить", "Rehacer", "Refazer", "Rétablir"}},
    {UiText::menu_cut, {"剪切", "Cut", "剪下", "カット", "오려두기", "Вырезать", "Cortar", "Recortar", "Couper"}},
    {UiText::menu_copy, {"拷贝", "Copy", "拷貝", "コピー", "복사하기", "Копировать", "Copiar", "Copiar", "Copier"}},
    {UiText::menu_paste, {"粘贴", "Paste", "貼上", "ペースト", "붙여넣기", "Вставить", "Pegar", "Colar", "Coller"}},
    {UiText::menu_select_all, {"全选", "Select All", "全選", "すべてを選択", "전체 선택", "Выбрать все", "Seleccionar todo", "Selecionar tudo", "Tout sélectionner"}},
    {UiText::menu_window, {"窗口", "Window", "視窗", "ウインドウ", "윈도우", "Окно", "Ventana", "Janela", "Fenêtre"}},
    {UiText::menu_minimize, {"最小化", "Minimize", "縮到最小", "しまう", "최소화", "Свернуть", "Minimizar", "Minimizar", "Réduire"}},
    {UiText::menu_close, {"关闭", "Close", "關閉", "閉じる", "닫기", "Закрыть", "Cerrar", "Fechar", "Fermer"}},
    {UiText::tray_show, {"显示 OGPlay", "Show OGPlay", "顯示 OGPlay", "OGPlay を表示", "OGPlay 보기", "Показать OGPlay", "Mostrar OGPlay", "Mostrar OGPlay", "Afficher OGPlay"}},
    {UiText::tray_quit, {"退出 OGPlay", "Quit OGPlay", "結束 OGPlay", "OGPlay を終了", "OGPlay 종료", "Завершить OGPlay", "Salir de OGPlay", "Encerrar OGPlay", "Quitter OGPlay"}},
    {UiText::pick_data_dir, {"选择游戏数据包目录", "Choose Game Data Folder", "選擇遊戲資料包資料夾", "ゲームデータフォルダを選択", "게임 데이터 폴더 선택", "Выберите папку с данными игры", "Elige la carpeta de datos del juego", "Escolha a pasta de dados do jogo", "Choisir le dossier de données du jeu"}},
    {UiText::pick_apk, {"选择 APK 或游戏压缩包", "Choose an APK or game archive", "選擇 APK 或遊戲壓縮檔", "APK またはゲームのアーカイブを選択", "APK 또는 게임 압축 파일 선택", "Выберите APK или архив с игрой", "Elige un APK o un archivo comprimido del juego", "Escolha um APK ou um arquivo compactado do jogo", "Choisir un APK ou une archive de jeu"}},
    {UiText::key_editor_window_title, {"OGPlay 按键设置", "OGPlay Key Settings", "OGPlay 按鍵設定", "OGPlay キー設定", "OGPlay 키 설정", "OGPlay — настройка клавиш", "OGPlay: configuración de teclas", "OGPlay – Configuração de teclas", "OGPlay – Configuration des touches"}},
});
// clang-format on

consteval bool Complete() {
    for (std::size_t i = 0; i < kUiLanguages.size(); ++i)
        if (static_cast<std::size_t>(kUiLanguages[i]) != i || kTags[i].empty()) return false;
    if (kText.size() != static_cast<std::size_t>(UiText::count)) return false;
    for (std::size_t i = 0; i < kText.size(); ++i) {
        if (static_cast<std::size_t>(kText[i].id) != i) return false;
        for (const auto text : kText[i].text)
            if (text.empty()) return false;
    }
    return true;
}

static_assert(Complete());

std::size_t Column(UiLanguage language) {
    const auto index = static_cast<std::size_t>(language);
    if (index >= kLanguageCount) throw std::invalid_argument("unknown UI language");
    return index;
}

char Lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool Equals(std::string_view left, std::string_view right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) { return Lower(a) == Lower(b); });
}
}  // namespace

UiMessageSource::~UiMessageSource() = default;

const UiMessage* FindUiMessage(const std::exception& error) noexcept {
    const auto* source = dynamic_cast<const UiMessageSource*>(&error);
    return source && !source->Message().id.empty() ? &source->Message() : nullptr;
}

std::string_view Text(UiLanguage language, UiText id) {
    const auto& row = kText.at(static_cast<std::size_t>(id));
    return row.text[Column(language)];
}

std::optional<UiLanguage> ParseUiLanguage(std::string_view text) {
    for (std::size_t i = 0; i < kTags.size(); ++i)
        if (text == kTags[i]) return kUiLanguages[i];
    return std::nullopt;
}

std::string_view UiLanguageTag(UiLanguage language) {
    return kTags[Column(language)];
}

UiLanguage UiLanguageForLocale(std::string_view locale) {
    const auto end = locale.find_first_of("-_");
    const auto base = locale.substr(0, end);
    if (Equals(base, "zh")) {
        // Traditional script, or a region that writes it (zh-Hant-*, zh-TW, zh-HK, zh-MO).
        for (auto rest = end == std::string_view::npos ? std::string_view{} : locale.substr(end + 1); !rest.empty();) {
            const auto next = rest.find_first_of("-_");
            const auto part = rest.substr(0, next);
            if (Equals(part, "hant") || Equals(part, "tw") || Equals(part, "hk") || Equals(part, "mo"))
                return UiLanguage::zh_TW;
            if (next == std::string_view::npos) break;
            rest = rest.substr(next + 1);
        }
        return UiLanguage::zh_CN;
    }
    if (Equals(base, "ja")) return UiLanguage::ja_JP;
    if (Equals(base, "ko")) return UiLanguage::ko_KR;
    if (Equals(base, "ru")) return UiLanguage::ru_RU;
    if (Equals(base, "es")) return UiLanguage::es_ES;
    if (Equals(base, "pt")) return UiLanguage::pt_BR;
    if (Equals(base, "fr")) return UiLanguage::fr_FR;
    return UiLanguage::en_US;
}

UiLanguage SystemUiLanguage() {
    return UiLanguageForLocale(hal::HostPreferredLocale());
}

UiLanguage ResolveUiLanguage(std::string_view setting) {
    if (setting == "system") return SystemUiLanguage();
    if (const auto value = ParseUiLanguage(setting)) return *value;
    throw std::invalid_argument("language must be system or a supported language tag");
}
}  // namespace ogplay::app
