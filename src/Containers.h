#pragma once
#include "DetectedProblems.h"
#include "Offsets.h"
#include "Scheduler.h"
#include "Settings.h"

#include "StateData.h"

#include <rapidjson/document.h>
#include <unordered_set>

class SubMod;

template <typename T, typename Derived>
class Set
{
public:
	using Callback = std::function<void()>;

	Set() = default;

	Set(SubMod* a_parentSubMod) :
		_parentSubMod(a_parentSubMod) {}

	bool IsEmpty() const { return _entries.empty(); }

	bool IsDirty() const { return _bDirty; }

	void SetDirty(bool a_bDirty)
	{
		_bDirty = a_bDirty;
		// A row added, removed, replaced or edited: the program compiles again on the next tick
		if (a_bDirty) {
			Scheduler::Recompile();
		}

		for (auto& callback : _onDirtyCallbacks) {
			callback();
		}
	}

	bool IsValid() const
	{
		ReadLocker locker(_lock);

		return std::ranges::all_of(_entries, [&](auto& entry) { return entry->IsValid(); });
	}

	RE::BSVisit::BSVisitControl ForEach(const std::function<RE::BSVisit::BSVisitControl(std::unique_ptr<T>&)>& a_func)
	{
		using Result = RE::BSVisit::BSVisitControl;

		ReadLocker locker(_lock);

		auto result = Result::kContinue;

		for (auto& entry : _entries) {
			result = a_func(entry);
			if (result == Result::kStop) {
				return result;
			}
		}

		return result;
	}

	void Add(std::unique_ptr<T>& a_object, bool a_bSetDirty = false)
	{
		if (!a_object) {
			return;
		}

		{
			WriteLocker locker(_lock);
			auto& entry = _entries.emplace_back(std::move(a_object));
			SetAsParent(entry);
		}

		if (a_bSetDirty) {
			SetDirty(true);
		}
	}

	void Remove(const std::unique_ptr<T>& a_object)
	{
		if (!a_object) {
			return;
		}

		{
			WriteLocker locker(_lock);
			std::erase(_entries, a_object);
		}

		SetDirty(true);

		auto& detectedProblems = DetectedProblems::GetSingleton();
		if (detectedProblems.HasSubModsWithInvalidEntries()) {
			detectedProblems.CheckForSubModsWithInvalidEntries();
		}
	}

	std::unique_ptr<T> Extract(std::unique_ptr<T>& a_object)
	{
		WriteLocker locker(_lock);

		auto extracted = std::move(a_object);
		std::erase(_entries, a_object);

		return extracted;
	}

	void Insert(std::unique_ptr<T>& a_objectToInsert, const std::unique_ptr<T>& a_insertAfter, bool a_bSetDirty = false)
	{
		if (!a_objectToInsert) {
			return;
		}

		{
			WriteLocker locker(_lock);

			bool bInserted = false;
			if (a_insertAfter) {
				if (const auto it = std::ranges::find(_entries, a_insertAfter); it != _entries.end()) {
					const auto entry = _entries.insert(it + 1, std::move(a_objectToInsert));
					SetAsParent(*entry);
					bInserted = true;
				}
			}
			if (!bInserted) {
				auto& entry = _entries.emplace_back(std::move(a_objectToInsert));
				SetAsParent(entry);
			}
		}

		// Once it is in: what listens to the set reads it with the new entry
		if (a_bSetDirty) {
			SetDirty(true);
		}
	}

	void Replace(std::unique_ptr<T>& a_objectToSubstitute, std::unique_ptr<T>& a_newObject)
	{
		if (a_objectToSubstitute == nullptr || a_newObject == nullptr) {
			return;
		}

		if (a_objectToSubstitute == a_newObject) {
			return;  // same object - nothing to do
		}

		{
			WriteLocker locker(_lock);
			SetAsParent(a_newObject);
			a_objectToSubstitute = std::move(a_newObject);
		}

		SetDirty(true);

		auto& detectedProblems = DetectedProblems::GetSingleton();
		if (detectedProblems.HasSubModsWithInvalidEntries()) {
			detectedProblems.CheckForSubModsWithInvalidEntries();
		}
	}

	void Move(std::unique_ptr<T>& a_sourceObject, Set<T, Derived>* a_sourceSet, const std::unique_ptr<T>& a_targetObject, bool a_bInsertAfter = false)
	{
		if (a_sourceObject == a_targetObject) {
			return;  // same object - nothing to do
		}

		if (a_sourceSet == this) {
			// move within the same set
			WriteLocker locker(_lock);

			const auto sourceIndex = std::distance(_entries.begin(), std::ranges::find(_entries, a_sourceObject));
			auto targetIndex = std::distance(_entries.begin(), std::ranges::find(_entries, a_targetObject));

			if (a_bInsertAfter) {
				++targetIndex;
			}

			if (sourceIndex < targetIndex) {
				--targetIndex;
			}

			if (sourceIndex == targetIndex) {
				return;
			}

			auto extractedObject = std::move(a_sourceObject);
			_entries.erase(_entries.begin() + sourceIndex);
			_entries.insert(_entries.begin() + targetIndex, std::move(extractedObject));

			SetDirty(true);
		} else {
			// move from other set

			// make sure the source object is not an ancestor of this set
			if (IsChildOf(a_sourceObject.get())) {
				return;
			}

			WriteLocker locker(_lock);
			const auto targetIt = std::ranges::find(_entries, a_targetObject);
			auto extractedObject = a_sourceSet->Extract(a_sourceObject);
			if (targetIt != _entries.end()) {
				if (a_bInsertAfter) {
					const auto& entry = _entries.insert(targetIt + 1, std::move(extractedObject));
					SetAsParent(*entry);
				} else {
					const auto& entry = _entries.insert(targetIt, std::move(extractedObject));
					SetAsParent(*entry);
				}
			} else {
				auto& entry = _entries.emplace_back(std::move(extractedObject));
				SetAsParent(entry);
			}

			SetDirty(true);
		}
	}

	void MoveAll(Set<T, Derived>* a_otherSet)
	{
		WriteLocker locker(_lock);

		a_otherSet->ForEach([this](auto& entry) {
			SetAsParent(entry);
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		_entries = std::move(a_otherSet->_entries);
	}

	void Append(Set<T, Derived>* a_otherSet)
	{
		WriteLocker locker(_lock);

		a_otherSet->ForEach([this](auto& entry) {
			SetAsParent(entry);
			return RE::BSVisit::BSVisitControl::kContinue;
		});

		WriteLocker otherLocker(a_otherSet->_lock);

		_entries.reserve(_entries.size() + a_otherSet->_entries.size());

		_entries.insert(_entries.end(), std::make_move_iterator(a_otherSet->_entries.begin()), std::make_move_iterator(a_otherSet->_entries.end()));

		SetDirty(true);
	}

	void Clear()
	{
		{
			WriteLocker locker(_lock);
			_entries.clear();
		}

		SetDirty(true);

		auto& detectedProblems = DetectedProblems::GetSingleton();
		if (detectedProblems.HasSubModsWithInvalidEntries()) {
			detectedProblems.CheckForSubModsWithInvalidEntries();
		}
	}

	void SetAsParent(std::unique_ptr<T>& a_object)
	{
		static_cast<Derived*>(this)->SetAsParentImpl(a_object);
	}

	rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator)
	{
		rapidjson::Value entryArrayValue(rapidjson::kArrayType);

		ForEach([&](auto& entry) {
			rapidjson::Value entryValue(rapidjson::kObjectType);
			entry->Serialize(static_cast<void*>(&entryValue), static_cast<void*>(&a_allocator));
			entryArrayValue.PushBack(entryValue, a_allocator);

			return RE::BSVisit::BSVisitControl::kContinue;
		});

		return entryArrayValue;
	}

	bool IsChildOf(T* a_object)
	{
		return static_cast<Derived*>(this)->IsChildOfImpl(a_object);
	}

	size_t Num() const { return _entries.size(); }

	[[nodiscard]] std::string NumText() const
	{
		return static_cast<const Derived*>(this)->NumTextImpl();
	}

	[[nodiscard]] SubMod* GetParentSubMod() const
	{
		return static_cast<const Derived*>(this)->GetParentSubModImpl();
	}

	[[nodiscard]] bool IsDirtyRecursive() const
	{
		return static_cast<const Derived*>(this)->IsDirtyRecursiveImpl();
	}

	void SetDirtyRecursive(bool a_bDirty)
	{
		static_cast<Derived*>(this)->SetDirtyRecursiveImpl(a_bDirty);
	}

	void AddOnDirtyCallback(Callback a_callback)
	{
		_onDirtyCallbacks.push_back(a_callback);
	}

	void RemoveOnDirtyCallback(Callback a_callback)
	{
		_onDirtyCallbacks.erase(a_callback);
	}

protected:
	mutable SharedLock _lock;
	std::vector<std::unique_ptr<T>> _entries;
	std::vector<Callback> _onDirtyCallbacks;
	bool _bDirty = false;

	SubMod* _parentSubMod = nullptr;
};

