// NONG 2.1 - cambia la cancion de un nivel por un archivo local o una URL.
// Escrito contra Geode v1.3.10 (GD 2.113, Windows 32 bits).
#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/InputNode.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/modify/MusicDownloadManager.hpp>
#include <Geode/modify/CustomSongWidget.hpp>

using namespace geode::prelude;

namespace nong {
    static std::string keyFor(int songID) {
        return "song-" + std::to_string(songID);
    }

    // Devuelve la ruta personalizada de una cancion, o "" si no hay una valida.
    static std::string customPath(int songID) {
        auto mod = Mod::get();
        auto key = keyFor(songID);
        if (!mod->hasSavedValue(key)) return "";
        auto path = mod->getSavedValue<std::string>(key);
        if (path.empty()) return "";
        std::error_code ec;
        if (!ghc::filesystem::exists(ghc::filesystem::path(path), ec) || ec) return "";
        return path;
    }

    static void setCustom(int songID, std::string const& path) {
        Mod::get()->setSavedValue<std::string>(keyFor(songID), path);
    }

    static void notify(std::string const& text, NotificationIcon icon) {
        Notification::create(text, icon)->show();
    }

    // Avisa al juego de que el estado de la cancion cambio.
    static void refresh() {
        if (auto mgr = MusicDownloadManager::sharedState()) {
            mgr->songStateChanged();
        }
    }
}

// Si hay una cancion personalizada para este ID, el juego usa esa ruta.
struct $modify(NongMusicDownloadManager, MusicDownloadManager) {
    gd::string pathForSong(int id) {
        auto custom = nong::customPath(id);
        if (!custom.empty()) {
            return gd::string(custom.c_str());
        }
        return MusicDownloadManager::pathForSong(id);
    }
};

class NongPopup : public Popup<int> {
protected:
    int m_songID = 0;
    InputNode* m_urlInput = nullptr;
    CCLabelBMFont* m_status = nullptr;

    bool setup(int songID) override {
        m_songID = songID;
        this->setTitle("NONG - Cancion " + std::to_string(songID));

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto center = CCPoint { winSize.width / 2, winSize.height / 2 };

        m_status = CCLabelBMFont::create("", "chatFont.fnt");
        m_status->setScale(.8f);
        m_status->setPosition(center.x, center.y + 45.f);
        m_mainLayer->addChild(m_status);
        this->updateStatus();

        m_urlInput = InputNode::create(260.f, "URL directa a un .mp3");
        m_urlInput->setPosition(center.x, center.y + 10.f);
        m_mainLayer->addChild(m_urlInput);

        auto makeBtn = [&](char const* text, SEL_MenuHandler cb, CCPoint pos) {
            auto spr = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_01.png", .6f);
            auto btn = CCMenuItemSpriteExtra::create(spr, this, cb);
            btn->setPosition(pos);
            m_buttonMenu->addChild(btn);
        };
        // m_buttonMenu (CCMenu) ya esta centrado en la pantalla
        makeBtn("Descargar", menu_selector(NongPopup::onDownload), { -70.f, -30.f });
        makeBtn("Archivo",   menu_selector(NongPopup::onPickFile), {  40.f, -30.f });
        makeBtn("Quitar",    menu_selector(NongPopup::onReset),    {   0.f, -65.f });
        return true;
    }

    void updateStatus() {
        auto path = nong::customPath(m_songID);
        m_status->setString(path.empty() ? "Usando la cancion original" : "Cancion personalizada activa");
    }

    void onPickFile(CCObject*) {
        file::FilePickOptions opts;
        opts.filters.push_back({ "Audio", { "*.mp3", "*.ogg", "*.wav" } });
        auto res = file::pickFile(file::PickMode::OpenFile, opts);
        if (!res) {
            nong::notify("No se pudo abrir el selector", NotificationIcon::Error);
            return;
        }
        auto path = res.unwrap();
        if (path.empty()) return;
        nong::setCustom(m_songID, path.string());
        nong::refresh();
        this->updateStatus();
        nong::notify("Cancion aplicada", NotificationIcon::Success);
    }

    void onDownload(CCObject*) {
        std::string url = m_urlInput->getString();
        if (url.empty()) {
            nong::notify("Escribe una URL primero", NotificationIcon::Warning);
            return;
        }
        auto dir = Mod::get()->getSaveDir();
        std::error_code ec;
        ghc::filesystem::create_directories(dir, ec);
        auto path = dir / (std::to_string(m_songID) + ".mp3");
        int id = m_songID;
        nong::notify("Descargando...", NotificationIcon::Loading);
        web::AsyncWebRequest()
            .fetch(url)
            .into(path)
            .then([id, path](std::monostate) {
                nong::setCustom(id, path.string());
                nong::refresh();
                nong::notify("Descarga completa", NotificationIcon::Success);
            })
            .expect([](std::string const& err) {
                nong::notify("Fallo la descarga: " + err, NotificationIcon::Error);
            });
    }

    void onReset(CCObject*) {
        nong::setCustom(m_songID, "");
        nong::refresh();
        this->updateStatus();
        nong::notify("Cancion original restaurada", NotificationIcon::Success);
    }

public:
    static NongPopup* create(int songID) {
        auto ret = new NongPopup();
        if (ret && ret->init(300.f, 190.f, songID)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

// Agrega un boton "NONG" al widget de cancion personalizada.
struct $modify(NongCustomSongWidget, CustomSongWidget) {
    bool init(SongInfoObject* info, LevelSettingsObject* settings, bool a, bool b, bool c, bool d, bool e) {
        if (!CustomSongWidget::init(info, settings, a, b, c, d, e)) return false;

        auto spr = ButtonSprite::create("NONG", "bigFont.fnt", "GJ_button_04.png", .5f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(NongCustomSongWidget::onNong));
        // Posicion aproximada; ajustar tras probar en el juego.
        if (m_moreBtn) {
            btn->setPosition(m_moreBtn->getPositionX() - 40.f, m_moreBtn->getPositionY());
        } else {
            btn->setPosition(0.f, -40.f);
        }
        m_buttonMenu->addChild(btn);
        return true;
    }

    void onNong(CCObject*) {
        int id = m_songInfo ? m_songInfo->m_songID : m_customSongID;
        if (id <= 0) {
            nong::notify("Este nivel usa una cancion oficial", NotificationIcon::Warning);
            return;
        }
        NongPopup::create(id)->show();
    }
};
