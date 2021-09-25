# Kmem
$description

# Informations
**Developer:** *CODEX-DR*

**Submitted by:** *CODEX-DR*

# ABOUT THIS


<div align="center">
    <img src="https://githacks.org/_xeroxz/kmem/-/raw/f9d610783e8699406bf47e2d443813f4b976c4bf/img/kmem-icon.png"/>
</div>

# /proc/kmem

/proc/kmem reimplementation for windows. Instead of an illusionary file, its a process named "/proc/kmem" which you can see in task manager + use CreateToolhelp32Snapshot to obtain the PID. 
On windows I was told it was "impossible to name your process something other then the executables name" so I figured I would go the extra mile and throw in some forward slashes into the name
since you dont see that very often on windows.

# how is this possible

I copy the kernel pml4e's into the usermode part of the pml4. Windows has an amazing infrastructure/API that can be used to do much more then its been allowed. Lets take a look
at ReadProcessMemory. ReadProcessMemory calls NtReadVirtualMemory which then calls MmCopyVirtualMemory. NtReadVirtualMemory does validation checks on addresses supplied from
usermode ensuring that they are indeed usermode addresses and not kernel addresses. They dont check paging tables for this they just have an if statement. This means if you 
copy the top 256 pml4e's into the bottom (usermode) part of the pml4, NtReadVirtualMemory will work on this memory although it is indeed kernel memory still.

```cpp
  if ( v11 )
  {
    if ( &a2[a4] < a2 || (unsigned __int64)&a2[a4] > 0x7FFFFFFF0000i64 || a3 + a4 < a3 || a3 + a4 > 0x7FFFFFFF0000i64 )
      return 0xC0000005i64;
    v12 = (_QWORD *)a5;
    if ( a5 )
    {
      v13 = (_QWORD *)a5;
      if ( a5 >= 0x7FFFFFFF0000i64 )
        v13 = (_QWORD *)0x7FFFFFFF0000i64;
      *v13 = *v13;
    }
  }
  else
  {
    v12 = (_QWORD *)a5;
  }
```

# how do I use this

Simply obtain the pid of `/proc/kmem` and then you can `OpenProcess` with `PROCESS_ALL_ACCESS`. You will be able to ReadProcessMemory and WriteProcessMemory, you will not
be able to VirtualProtectEx, instead you can walk the paging tables since the self referencing pml4e is in usermode! You can scan for the self referencing pml4e or keep a 
reference to the index it is located at. That is up to you!

### address translation

As stated in `how is this possible`, NtReadVirtualMemory is going to ensure that the addresses you are passing land below pml4e 256 (counting from 0 here). In order
to still use windows api, you must call `nasa::kmem_ctx::translate`. `translate` is a static function that simply subtracts the pml4 index by 256 in order to give you the usermode mappings
of the kernel memory which you can then use with ReadProcessMemory/WriteProcessMemory...

```cpp
auto kmem_ctx::translate(std::uintptr_t kva) -> std::uintptr_t
{
	virt_addr_t old_addr{ reinterpret_cast<void*>(kva) };
	virt_addr_t new_addr{ NULL };
	new_addr.pml4_index = old_addr.pml4_index - 255;
	new_addr.pdpt_index = old_addr.pdpt_index;
	new_addr.pd_index = old_addr.pd_index;
	new_addr.pt_index = old_addr.pt_index;
	return reinterpret_cast<std::uintptr_t>(new_addr.value);
}
```

### Example

```cpp
auto kmem_handle = nasa::kmem_ctx::get_handle();
unsigned short mz = 0u;
std::size_t bytes_handled;

// ReadProcessMemory kernel memory example...
result = ReadProcessMemory(
	kmem_handle,
	reinterpret_cast<void*>(ntoskrnl_translated),
	&mz, sizeof mz,
	&bytes_handled
);


std::printf("[+] ReadProcessMemory Result -> %d, mz -> 0x%x\n", result, mz);
```

```
[+] ntoskrnl base -> 0xFFFFF8024A600000
[+] ntoskrnl translated -> 0x000078824A600000
[+] set manager pethread -> 0xFFFF848887775040
[+] suspend thread result -> 0x0000000000000000
[+] ReadProcessMemory Result -> 1, mz -> 0x5a4d
[+] press enter to exit...
```
# Warning

WriteProcessMemory does not work right now since NtWriteVirtualMemory --> MmCopyVirtualMemory --> MmProbeAndLockPages <--- this fails. 
