#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "engine/rendering/cache/ResourceSlot.h"

namespace engine::rendering::cache
{

/**
 * @brief Lightweight identity tracker for cached bind groups.
 *
 * A signature is a vector of `(pointer, version)` entries, one per resource
 * the cached bind group references. Comparing two signatures answers a
 * single question: *"is every constituent still the same identity as the
 * last time I rebuilt?"* Bind-group caching call sites store the signature
 * alongside the cached bind group; each frame they recompute the current
 * signature, compare with the stored one, and rebuild only on mismatch.
 *
 * **Why this exists.** A factory slot can swap its resource in place via
 * `SlotCache::replace` (hot reload), or replace it via `clearResources` +
 * lazy rebuild on next access. In both cases the wrapper pointer changes,
 * but a hand-rolled "did the GBuffer pointer change?" check that compares
 * the *owner* of the resource (e.g. the `GBuffer*` whose textures got
 * swapped in `GBuffer::resize`) misses the swap entirely and the bind
 * group keeps sampling destroyed views. The signature pattern fixes this
 * by tracking the *referenced resources* themselves, not their owner.
 *
 * **What "identity" means.** The raw pointer of the C++ wrapper object
 * (`WebGPUTexture *`, `WebGPUBuffer *`, `WebGPUSampler *`). When a factory
 * replaces a resource via `SlotCache::replace`, the wrapper pointer
 * changes — signature picks this up. When a factory soft-clears, an
 * outstanding consumer `shared_ptr` keeps the wrapper alive at the same
 * pointer; signature says "no rebuild needed" until the consumer itself
 * refetches a fresh wrapper.
 *
 * **Version channel.** For `Handle<T>`-backed resources, use
 * `addVersioned()` to record the slot's monotonic `version()`. That picks
 * up in-place swaps even if the wrapper pointer is reused. For raw
 * shared_ptr resources, the pointer comparison alone is enough.
 *
 * **Why a vector instead of a hash.** Bind groups have at most a few dozen
 * bindings, so `O(n)` linear compare is trivial. A hash could collide and
 * silently skip a rebuild — the bug class this primitive exists to fix.
 *
 * **Thread safety.** None. Caller-side single-threaded use (each cached
 * bind group has its own signature; the renderer touches them one camera
 * at a time on the render thread).
 *
 * @see SlotCache, Handle::version
 */
class BindGroupSignature
{
  public:
	BindGroupSignature() = default;

	/// Drop every recorded entry. Call before re-appending the current
	/// constituents.
	void clear() { m_entries.clear(); }

	/// True iff nothing has been recorded yet — convenient for the
	/// first-build path ("if the cached signature is empty, just build").
	[[nodiscard]] bool empty() const { return m_entries.empty(); }

	/// Number of entries recorded.
	[[nodiscard]] std::size_t size() const { return m_entries.size(); }

	/// Append a resource identity (pointer only). Order matters — record
	/// constituents in the same order on every recompute so signatures
	/// compare correctly.
	void add(const void *p) { m_entries.push_back({p, 0}); }

	/// Append with an explicit version. Use this for resources whose
	/// `Handle<T>::version()` can move while the wrapper pointer stays
	/// the same (rare, but possible with `SlotCache::replace` reusing
	/// an allocation).
	void addVersioned(const void *p, uint32_t version) { m_entries.push_back({p, version}); }

	/// Convenience overload for any `shared_ptr<T>` resource — records the
	/// underlying wrapper pointer. nullptr-safe (records the zero pointer
	/// so a later non-null swap triggers a rebuild).
	template <typename T>
	void add(const std::shared_ptr<T> &p) { add(static_cast<const void *>(p.get())); }

	/// Convenience overload for a `Handle<T>` resource — records BOTH the
	/// currently-locked wrapper pointer AND the slot's monotonic version().
	/// This is the version-aware path: even if `SlotCache::replace` happens
	/// to reuse the same wrapper allocation (so the pointer stays equal),
	/// the bumped version still triggers a rebuild. The slot is `lock()`-ed
	/// here, which counts as an access and resets the slot's idle-eviction
	/// timer — appropriate, since we're about to bake its resource into a
	/// bind group.
	template <typename T>
	void addVersioned(const Handle<T> &handle)
	{
		auto snap = handle.lock();
		addVersioned(static_cast<const void *>(snap.get()), handle.version());
	}

	[[nodiscard]] bool operator==(const BindGroupSignature &other) const
	{
		return m_entries == other.m_entries;
	}

	[[nodiscard]] bool operator!=(const BindGroupSignature &other) const
	{
		return !(*this == other);
	}

  private:
	struct Entry
	{
		const void *ptr     = nullptr;
		uint32_t    version = 0;

		[[nodiscard]] bool operator==(const Entry &o) const noexcept
		{
			return ptr == o.ptr && version == o.version;
		}
	};

	std::vector<Entry> m_entries;
};

} // namespace engine::rendering::cache
