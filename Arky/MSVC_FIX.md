# MSVC C2589 (`min` / `max` macro collision)

**Cause:** a Windows SDK header or another transitive dependency can define the `min` / `max` function-like macros. The compiler then expands `std::min(...)` into invalid C++, producing `error C2589: '(' illegal token on right side of '::'`.

**Fix:**
- Windows Xmake configuration now defines `NOMINMAX` globally.
- Every `std::min` expression uses `(std::min)(...)` (or `(std::min<T>)(...)`), which is immune to the preprocessor macro.
- Every affected `numeric_limits<T>::max()` expression uses `(std::numeric_limits<T>::max)()`.
- `archive_engine.cpp` protects the local `NOMINMAX` definition against redefinition.

Rebuild on Windows using:

```powershell
xmake f -p windows -a x64 -m release -y
xmake -r
xmake run Arky
```

**Verification:** portable archive and transfer tests ran on Linux. The actual Windows MSVC / Qt build still needs to execute on Windows.
