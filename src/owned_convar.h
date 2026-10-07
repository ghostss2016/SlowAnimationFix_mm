#pragma once

#include <memory>
#include <optional>
#include <utility>

namespace slow_animation {

// The SDK queues pointers to name-constructed references until RegisterAll.
// Keep this module's reference alive through UnregisterAll, including a failed
// Load. Api is the small SDK boundary; the same owner is used by native tests.
template <class Api>
class OwnedConVarReference {
    using Reference = typename Api::Reference;
    Api api_;
    std::unique_ptr<Reference> reference_;
    bool registered_ = false;

public:
    explicit OwnedConVarReference(Api api = {}) : api_(std::move(api)) {}
    OwnedConVarReference(const OwnedConVarReference&) = delete;
    OwnedConVarReference& operator=(const OwnedConVarReference&) = delete;
    ~OwnedConVarReference() { Reset(); }

    bool Acquire(const char* name) {
        if (reference_ || !api_.Available()) return false;
        // Construct BEFORE registering: a reference made after RegisterAll is
        // registered immediately and is not retained in the SDK cleanup list.
        reference_ = api_.Create(name);
        if (!reference_) return false;
        registered_ = true;
        api_.Register();
        if (!reference_->IsValidRef()) {
            Reset();
            return false;
        }
        return true;
    }

    void Reset() noexcept {
        if (registered_) {
            api_.Unregister();
            registered_ = false;
        }
        reference_.reset();
    }

    Reference* GetReference() const {
        return reference_ && reference_->IsValidRef() ? reference_.get() : nullptr;
    }

    class LoadGuard {
        OwnedConVarReference& owner_;
        bool committed_ = false;
    public:
        explicit LoadGuard(OwnedConVarReference& owner) : owner_(owner) {}
        LoadGuard(const LoadGuard&) = delete;
        LoadGuard& operator=(const LoadGuard&) = delete;
        ~LoadGuard() { if (!committed_) owner_.Reset(); }
        void Commit() { committed_ = true; }
    };

    LoadGuard RollbackUnlessCommitted() { return LoadGuard(*this); }
};

template <class Api>
std::optional<float> ReadTimelimit(const OwnedConVarReference<Api>& owner) {
    const auto* reference = owner.GetReference();
    return reference ? std::optional<float>(reference->Get()) : std::nullopt;
}

template <class Api>
bool WriteTimelimit(OwnedConVarReference<Api>& owner, float limit) {
    auto* reference = owner.GetReference();
    if (!reference) return false;
    reference->Set(limit);
    return true;
}

} // namespace slow_animation
