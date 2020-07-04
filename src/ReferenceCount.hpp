#pragma once
#include <functional>
#include <atomic>
#include <unordered_map>

/*
 * We implement our own reference counting rather than relying on std::shared_ptr,
 * since we prefer to have the control block contained within the object itself
 * thereby reducing overhead and fragmentation
 */

class ReferenceCounted {
public:
	inline ReferenceCounted() : _refcnt(1) { }
	inline ReferenceCounted(const ReferenceCounted& other) : _refcnt(1) { }
	inline virtual ~ReferenceCounted() { }

	inline void ref() { std::atomic_fetch_add(&_refcnt, 1); }
	inline void unref() { if (std::atomic_fetch_sub(&_refcnt, 1) == 1) { delete this; } }

	std::atomic<unsigned int> _refcnt;
};

template <class T> class rfc_ptr {
public:
	T *lpNode;

	inline T* operator->() { return lpNode; }
	inline T& operator*() { return *lpNode; }

	inline const T* operator->() const { return lpNode; }
	inline const T& operator*() const { return *lpNode; }

	inline rfc_ptr() : lpNode(NULL) { }
	inline rfc_ptr(const rfc_ptr &lpOther) {
		lpNode = lpOther.lpNode;
		if (lpNode) { lpNode->ref(); }
	}
	inline rfc_ptr(nullptr_t) : lpNode(NULL) { }
	template<typename... Args> static inline rfc_ptr create(Args&&... args) {
		rfc_ptr lpOutput(new T(std::forward<Args>(args)...));
		return lpOutput;
	}
	template<typename S> static inline rfc_ptr<T> typecast(const rfc_ptr<S> &lpOther) {
		rfc_ptr<T> lpOutput((T *)lpOther.lpNode);
		lpOutput->ref();
		return lpOutput;
	}
	template<typename S> static inline rfc_ptr<T> typecast(const S* lpOther) {
		rfc_ptr<T> lpOutput((T *)lpOther);
		lpOutput->ref();
		return lpOutput;
	}
	inline ~rfc_ptr() { if (lpNode) { lpNode->unref(); } }

	inline rfc_ptr<T>& operator=(const rfc_ptr<T>& lpOther) {
		if (lpNode != lpOther.lpNode) {
			if (lpNode) { lpNode->unref(); }
			lpNode = lpOther.lpNode;
			if (lpNode) { lpNode->ref(); }
		}
		return *this;
	}
	inline rfc_ptr<T>& operator=(T* lpOther) {
		if (lpNode != lpOther) {
			if (lpNode) { lpNode->unref(); }
			lpNode = lpOther;
			if (lpNode) { lpNode->ref(); }
		}
		return *this;
	}
	inline rfc_ptr<T>& operator=(nullptr_t) {
		if (lpNode) { lpNode->unref(); }
		lpNode = NULL;
		return *this;
	}
	inline bool operator==(const rfc_ptr<T> &lpOther) const { return lpNode == lpOther.lpNode; }
	inline bool operator==(const nullptr_t) const { return lpNode == NULL; }
	inline bool operator!=(const rfc_ptr<T> &lpOther) const { return lpNode != lpOther.lpNode; }
	inline bool operator!=(const nullptr_t) const { return lpNode != NULL; }
	inline bool operator<(const rfc_ptr<T> &lpOther) const { return lpNode < lpOther.lpNode; }

	inline operator bool() { return lpNode != NULL; }

protected:
	inline rfc_ptr(T *lpNode) : lpNode(lpNode) { }
};

namespace std {
	template <class T> struct hash<rfc_ptr<T>> {
		std::size_t operator()(const rfc_ptr<T> & t) const {
			return std::hash<void *>()(t.lpNode);
		}
	};
};
