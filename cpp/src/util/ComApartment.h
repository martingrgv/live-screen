#pragma once

#include <objbase.h>

// RAII wrapper around CoInitializeEx / CoUninitialize.
// Construct once near the top of wWinMain; destructor uninitializes the apartment
// only if the corresponding CoInitializeEx call succeeded.
class ComApartment
{
public:
	explicit ComApartment(DWORD coInitFlags) noexcept
		: hr_(CoInitializeEx(nullptr, coInitFlags))
	{
	}

	~ComApartment() noexcept
	{
		if (SUCCEEDED(hr_))
		{
			CoUninitialize();
		}
	}

	ComApartment(const ComApartment&) = delete;
	ComApartment& operator=(const ComApartment&) = delete;

	HRESULT hr() const noexcept { return hr_; }
	bool ok() const noexcept { return SUCCEEDED(hr_); }

private:
	HRESULT hr_;
};
