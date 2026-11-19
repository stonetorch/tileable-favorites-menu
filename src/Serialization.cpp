#include "Serialization.h"

#include "Layout.h"
#include "PreviewCache.h"
#include "logger.h"

namespace TFM::Serialization
{
    namespace
    {
        constexpr std::uint32_t FourCC(char a, char b, char c, char d)
        {
            return static_cast<std::uint32_t>(a) |
                (static_cast<std::uint32_t>(b) << 8U) |
                (static_cast<std::uint32_t>(c) << 16U) |
                (static_cast<std::uint32_t>(d) << 24U);
        }

        constexpr auto kPluginID = FourCC('T', 'F', 'M', 'N');
        constexpr auto kLegacy = FourCC('L', 'A', 'Y', 'T');
        constexpr auto kGrid = FourCC('G', 'R', 'I', 'D');
        constexpr auto kBackup = FourCC('G', 'B', 'A', 'K');
        constexpr std::uint32_t kVersion = 1;

        void Save(SKSE::SerializationInterface* serialization)
        {
            if (!Layout::GetSingleton().Save(serialization, kGrid, kBackup, kLegacy, kVersion)) {
                logger::error("Could not complete TFME layout serialization");
            }
        }

        void Load(SKSE::SerializationInterface* serialization)
        {
            auto& layout = Layout::GetSingleton();
            layout.Clear();
            std::uint32_t type = 0;
            std::uint32_t version = 0;
            std::uint32_t length = 0;
            bool loaded = false;
            bool backup = false;
            while (serialization->GetNextRecordInfo(type, version, length)) {
                if (version != kVersion) {
                    continue;
                }
                if (type == kLegacy) {
                    if (!layout.LoadLegacy(serialization)) {
                        logger::warn("Ignoring invalid legacy TFM layout");
                    }
                } else if (type == kGrid) {
                    loaded = layout.Load(serialization, length, false) || loaded;
                } else if (type == kBackup) {
                    backup = layout.Load(serialization, length, true) || backup;
                }
            }
            if (!loaded && backup) {
                layout.RestoreBackup();
            }
        }

        void Revert(SKSE::SerializationInterface*)
        {
            Layout::GetSingleton().Clear();
            PreviewCache::GetSingleton().Clear();
        }
    }

    bool Register()
    {
        const auto serialization = SKSE::GetSerializationInterface();
        if (!serialization) {
            logger::critical("SKSE serialization interface is unavailable");
            return false;
        }
        serialization->SetUniqueID(kPluginID);
        serialization->SetSaveCallback(Save);
        serialization->SetLoadCallback(Load);
        serialization->SetRevertCallback(Revert);
        return true;
    }
}
