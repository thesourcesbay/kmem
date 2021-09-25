#pragma once
#include "../mem_ctx/mem_ctx.hpp"

namespace nasa
{
	class kmem_ctx
	{
	public:
		kmem_ctx(vdm::vdm_ctx* vdm);
		static auto get_handle()->HANDLE;
		static auto translate(std::uintptr_t kva)->std::uintptr_t;
	private:
		void zombie(std::uint32_t pid) const;
		bool set_process_name(PEPROCESS peproc, const wchar_t* new_name);
		vdm::vdm_ctx* vdm;
	};
}