#include "kmem_ctx.hpp"

namespace nasa
{
	kmem_ctx::kmem_ctx(vdm::vdm_ctx* vdm)
		: vdm(vdm)
	{
		const auto runtime_broker_pid = 
			util::start_runtime_broker();

		zombie(runtime_broker_pid);
		const auto runtime_broker_peproc = 
			vdm->get_peprocess(runtime_broker_pid);

		set_process_name(runtime_broker_peproc, L"/proc/kmem");
		mem_ctx runtime_ctx(*vdm, runtime_broker_pid);

		const auto pml4 =
			reinterpret_cast<ppml4e>(
				runtime_ctx.set_page(
					runtime_ctx.get_dirbase()));

		// shoot the legs off the table, make it to do a back flip...
		// (aka put kernel pml4e's into usermode/below MmHighestUserAddress so NtReadVirtualMemory 
		// and NtWriteVirtualMemory/all winapis will work)...
		memcpy(pml4, pml4 + 255, 256 * sizeof pml4e);
	}

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

	auto kmem_ctx::get_handle()->HANDLE
	{
		return OpenProcess(PROCESS_ALL_ACCESS, FALSE, util::get_pid("/proc/kmem"));
	}

	void kmem_ctx::zombie(std::uint32_t pid) const
	{
		// zombie the the process by incrementing an exit counter
		// then calling TerminateProcess so the process never closes...
		const auto runtime_broker_peproc =
			reinterpret_cast<std::uintptr_t>(
				vdm->get_peprocess(pid));

		static const auto inc_ref_counter =
			util::get_kmodule_export(
				"ntoskrnl.exe",
				"PsAcquireProcessExitSynchronization"
			);

		const auto result =
			vdm->syscall<NTSTATUS(*)(std::uintptr_t)>(
				inc_ref_counter, runtime_broker_peproc);

		const auto handle = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
		TerminateProcess(handle, NULL);
		CloseHandle(handle);
	}

	bool kmem_ctx::set_process_name(PEPROCESS peproc, const wchar_t* new_name)
	{
		static const auto process_image_name =
			reinterpret_cast<std::uintptr_t>(
				util::get_kmodule_export(
					"ntoskrnl.exe", "SeLocateProcessImageName"));

		if (!process_image_name)
			return false;

		// SeLocateProcessImageName --> PsGetAllocatedFullProcessImageNameEx
	    // PAGE : 00000001406B1A50                  SeLocateProcessImageName proc near
		// PAGE : 00000001406B1A50 48 83 EC 28      sub     rsp, 28h
		// PAGE : 00000001406B1A54 E8 0B 00 00 00   call    PsGetAllocatedFullProcessImageNameEx <====== + 5
		// PAGE : 00000001406B1A59 48 83 C4 28      add     rsp, 28h
		// PAGE : 00000001406B1A5D C3               retn
		// PAGE : 00000001406B1A5D                  SeLocateProcessImageName endp
		const auto get_process_name =
			(process_image_name + 9 + vdm->rkm<std::uint32_t>(process_image_name + 5)); // <====== + 5

		// PAGE : 00000001406B1A64 48 83 EC 28                sub     rsp, 28h
		// PAGE : 00000001406B1A68 48 83 B9 10 07 00 00 00    cmp     qword ptr[rcx + 710h], 0
		// PAGE : 00000001406B1A70 B8 25 02 00 C0             mov     eax, 0C0000225h
		// PAGE : 00000001406B1A75 0F 85 0B E9 10 00          jnz     loc_1407C0386
		// PAGE : 00000001406B1A7B 48 83 B9 68 04 00 00 00    cmp     qword ptr[rcx + 468h], 0 <===== + 26
		const auto process_name_idx =
			vdm->rkm<std::uint32_t>(get_process_name + 26); // <===== + 26 

		// _SE_AUDIT_PROCESS_CREATION_INFO SeAuditProcessCreationInfo
		// _SE_AUDIT_PROCESS_CREATION_INFO --> POBJECT_NAME_INFORMATION == PUNICODE_STRING...
		const auto unicode_string_ptr = 
			vdm->rkm<std::uintptr_t>(
				reinterpret_cast<std::uintptr_t>(peproc) + process_name_idx);

		if (!unicode_string_ptr)
			return false;

		auto process_unicode_str = 
			vdm->rkm<UNICODE_STRING>(unicode_string_ptr);

		if (!process_unicode_str.Buffer)
			return false;

		// set new name...
		vdm->wkm((void*)process_unicode_str.Buffer, (void*)new_name, std::wcslen(new_name) * sizeof(wchar_t));

		// set new unicode string info...
		process_unicode_str.Length = std::wcslen(new_name) * sizeof(wchar_t);
		process_unicode_str.MaximumLength = std::wcslen(new_name) * sizeof(wchar_t);
		vdm->wkm<UNICODE_STRING>(unicode_string_ptr, process_unicode_str);
		return true;
	}
}