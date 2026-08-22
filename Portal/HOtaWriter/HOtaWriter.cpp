#include "HOtaWriter/HOtaWriter.hpp"

#define HLOG_MODULE_NAME "Ota"
#include <HLog/HLog.hpp>

#include <cstring>

#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace {

/** The IDF handle for a transfer in progress. Zero when there is none. */
esp_ota_handle_t otaHandle = 0;

/** The slot being written. Held across the transfer so finish() can name it. */
const esp_partition_t* slot = nullptr;

HOtaState currentState = HOtaState::Idle;
HOtaVerdict lastVerdict = HOtaVerdict::Ok;

HOtaImageInfo imageInfo = {};

size_t bytesWritten = 0;
size_t expectedTotal = 0;
bool downgradeAllowed = false;
bool sameVersionAllowed = true;

/**
 * The header, held back until it can be judged.
 *
 * This is what makes a refusal free: esp_ota_begin() erases the slot, and it is
 * not called until these bytes have passed. An image that is turned away leaves
 * the fallback firmware exactly where it was.
 */
uint8_t stage[HOTAWRITER_STAGE_BYTES];
size_t staged = 0;

/** True once the header passed and esp_ota_begin() has erased the slot. */
bool committed = false;

volatile bool rebootRequested = false;

/** @brief The running image's descriptor. Never null on a real device. */
const esp_app_desc_t* running() noexcept {
  return esp_app_get_description();
}

/**
 * @brief The project name a legitimate image must carry.
 *
 * Defaults to the running image's OWN name, which is right on every device and
 * needs configuring nowhere: an image only installs on the product it was built
 * for. HOTAWRITER_PROJECT_NAME overrides it, which is only worth doing to keep
 * accepting an old name through a rename.
 */
const char* expectedProject() noexcept {
#ifdef HOTAWRITER_PROJECT_NAME
  return HOTAWRITER_PROJECT_NAME;
#else
  const esp_app_desc_t* description = running();
  return (description != nullptr) ? description->project_name : "";
#endif
}

/** @brief Records a failure once, so the FIRST reason is the one reported. */
bool fail(HOtaVerdict why) noexcept {
  if (currentState != HOtaState::Failed) {
    lastVerdict = why;
    currentState = HOtaState::Failed;
    HWarning("update refused: %s", HOtaImage::verdictText(why));
  }

  // The handle is only open once the header passed, so this frees nothing on
  // the refusal path - which is exactly the case where nothing was erased.
  if (otaHandle != 0) {
    esp_ota_abort(otaHandle);
    otaHandle = 0;
  }

  return false;
}

/** @brief What this device is willing to accept. */
HOtaPolicy buildPolicy() noexcept {
  const esp_app_desc_t* description = running();

  HOtaPolicy policy = {};
  policy.expectedProject = expectedProject();
  policy.runningVersion = (description != nullptr) ? description->version : "";
  policy.expectedChipId = HOTAIMAGE_CHIP_ESP32C6;
  policy.partitionSize = (slot != nullptr) ? slot->size : 0;
  policy.allowSameVersion = sameVersionAllowed;
  policy.allowDowngrade = downgradeAllowed;
  return policy;
}

/**
 * @brief Judges the staged header and, if it passes, erases the slot.
 *
 * The one moment in a transfer where flash is touched for the first time.
 * @return false with the state left Failed when the image was refused.
 */
bool commitHeader() noexcept {
  lastVerdict = HOtaImage::inspect(stage, staged, expectedTotal, buildPolicy(), imageInfo);

  if (lastVerdict != HOtaVerdict::Ok) {
    // imageInfo is filled in even here, so a UI can say WHAT was turned away
    // rather than only that something was.
    HWarning("offered '%s' %s; running '%s' %s", imageInfo.projectName, imageInfo.version,
             expectedProject(), (running() != nullptr) ? running()->version : "?");
    return fail(lastVerdict);
  }

  // OTA_SIZE_UNKNOWN erases the whole partition, which is slower but correct
  // when the sender did not say how big the image is. A known size erases only
  // as far as it is needed, which is most of what makes a large update quick.
  const size_t eraseSize = (expectedTotal != 0) ? expectedTotal : OTA_SIZE_UNKNOWN;

  const esp_err_t started = esp_ota_begin(slot, eraseSize, &otaHandle);
  if (started != ESP_OK) {
    otaHandle = 0;
    HCritical("esp_ota_begin on %s failed: %s", slot->label, esp_err_to_name(started));
    return fail(HOtaVerdict::WriteFailed);
  }

  committed = true;
  HInfo("writing '%s' %s into %s", imageInfo.projectName, imageInfo.version, slot->label);

  const esp_err_t stageWrite = esp_ota_write(otaHandle, stage, staged);
  if (stageWrite != ESP_OK) {
    HCritical("esp_ota_write failed on the header: %s", esp_err_to_name(stageWrite));
    return fail(HOtaVerdict::WriteFailed);
  }

  bytesWritten = staged;
  return true;
}

}  // namespace

bool HOtaWriter::begin(size_t expectedSize, bool allowDowngrade,
                       bool allowSameVersion) noexcept {
  if (currentState == HOtaState::Writing) {
    HWarning("an update is already in progress");
    return false;
  }

  slot = esp_ota_get_next_update_partition(nullptr);
  if (slot == nullptr) {
    HCritical("no inactive OTA slot - check the partition table");
    currentState = HOtaState::Failed;
    lastVerdict = HOtaVerdict::WriteFailed;
    return false;
  }

  otaHandle = 0;
  staged = 0;
  bytesWritten = 0;
  committed = false;
  expectedTotal = expectedSize;
  downgradeAllowed = allowDowngrade;
  sameVersionAllowed = allowSameVersion;
  lastVerdict = HOtaVerdict::Ok;
  std::memset(&imageInfo, 0, sizeof(imageInfo));
  currentState = HOtaState::Writing;

  HInfo("update starting: %u bytes offered, target %s (%u bytes)",
        static_cast<unsigned>(expectedSize), slot->label, static_cast<unsigned>(slot->size));
  return true;
}

bool HOtaWriter::write(const uint8_t* data, size_t size) noexcept {
  if (currentState != HOtaState::Writing) {
    return false;
  }
  if (data == nullptr || size == 0) {
    return true;
  }

  // Fill the staging buffer first. Until it is full there is no verdict, and
  // until there is a verdict nothing may be erased.
  if (!committed) {
    const size_t room = sizeof(stage) - staged;
    const size_t take = (size < room) ? size : room;

    std::memcpy(stage + staged, data, take);
    staged += take;

    if (staged < sizeof(stage)) {
      // A whole image shorter than a header is not an image, but only finish()
      // knows the transfer has ended - so that is where it is caught.
      return true;
    }

    if (!commitHeader()) {
      return false;
    }

    data += take;
    size -= take;
    if (size == 0) {
      return true;
    }
  }

  const esp_err_t result = esp_ota_write(otaHandle, data, size);
  if (result != ESP_OK) {
    HCritical("esp_ota_write failed after %u bytes: %s", static_cast<unsigned>(bytesWritten),
              esp_err_to_name(result));
    return fail(HOtaVerdict::WriteFailed);
  }

  bytesWritten += size;
  return true;
}

bool HOtaWriter::finish() noexcept {
  if (currentState != HOtaState::Writing) {
    return false;
  }

  // Fewer bytes than a header ever arrived, so nothing was judged and nothing
  // was erased. An empty POST lands here.
  if (!committed) {
    return fail(HOtaVerdict::Incomplete);
  }

  // This is where the SHA-256 the build appended is verified. A truncated
  // upload or a connection that died halfway fails HERE, before anything is
  // marked bootable - which is what stops a half-written image being booted.
  const esp_err_t ended = esp_ota_end(otaHandle);
  otaHandle = 0;

  if (ended != ESP_OK) {
    HCritical("image verification failed: %s", esp_err_to_name(ended));
    currentState = HOtaState::Failed;
    lastVerdict = HOtaVerdict::VerifyFailed;
    return false;
  }

  const esp_err_t activated = esp_ota_set_boot_partition(slot);
  if (activated != ESP_OK) {
    HCritical("could not activate %s: %s", slot->label, esp_err_to_name(activated));
    currentState = HOtaState::Failed;
    lastVerdict = HOtaVerdict::WriteFailed;
    return false;
  }

  currentState = HOtaState::Done;
  HInfo("update ready: '%s' %s in %s, %u bytes - restart to run it", imageInfo.projectName,
        imageInfo.version, slot->label, static_cast<unsigned>(bytesWritten));
  return true;
}

void HOtaWriter::abort() noexcept {
  if (otaHandle != 0) {
    esp_ota_abort(otaHandle);
    otaHandle = 0;
  }

  if (currentState == HOtaState::Writing) {
    currentState = HOtaState::Idle;
    HWarning("update abandoned after %u bytes", static_cast<unsigned>(bytesWritten));
  }

  staged = 0;
  committed = false;
}

HOtaState HOtaWriter::state() noexcept {
  return currentState;
}

size_t HOtaWriter::written() noexcept {
  // Before the header is committed nothing has reached flash, but the staged
  // bytes HAVE been received - and a progress bar is about what arrived.
  return committed ? bytesWritten : staged;
}

size_t HOtaWriter::total() noexcept {
  return expectedTotal;
}

HOtaVerdict HOtaWriter::verdict() noexcept {
  return lastVerdict;
}

const char* HOtaWriter::reason() noexcept {
  return HOtaImage::verdictText(lastVerdict);
}

const HOtaImageInfo& HOtaWriter::offered() noexcept {
  return imageInfo;
}

const char* HOtaWriter::runningVersion() noexcept {
  const esp_app_desc_t* description = running();
  return (description != nullptr) ? description->version : "unknown";
}

const char* HOtaWriter::runningProject() noexcept {
  const esp_app_desc_t* description = running();
  return (description != nullptr) ? description->project_name : "unknown";
}

const char* HOtaWriter::slotName() noexcept {
  // Asked by the status route before any transfer exists, so it resolves the
  // slot itself rather than reporting nothing until an update starts.
  const esp_partition_t* target =
      (slot != nullptr) ? slot : esp_ota_get_next_update_partition(nullptr);
  return (target != nullptr) ? target->label : "none";
}

size_t HOtaWriter::slotSize() noexcept {
  const esp_partition_t* target =
      (slot != nullptr) ? slot : esp_ota_get_next_update_partition(nullptr);
  return (target != nullptr) ? target->size : 0;
}

void HOtaWriter::requestReboot() noexcept {
  HInfo("restart requested to run the new image");
  rebootRequested = true;
}

bool HOtaWriter::isRebootRequested() noexcept {
  return rebootRequested;
}
