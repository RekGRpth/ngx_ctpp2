
/*
 * Copyright (C) Valentin V. Bartenev
 */


#ifndef _CTPP2_NGINX_VM_ENVIRONMENT_HPP__
#define _CTPP2_NGINX_VM_ENVIRONMENT_HPP__ 1

#include <ctpp2/CTPP2SyscallFactory.hpp>
#include <ctpp2/CTPP2VM.hpp>

using namespace CTPP;

namespace CTPPNginx { // CT++ Module for Nginx

class NginxVMEnvironment {
	public:
		NginxVMEnvironment(
			const UINT_32  iStepsLimit,
			const UINT_32  iIMaxHandlers,
			const UINT_32  iIMaxArgStackSize,
			const UINT_32  iIMaxCodeStackSize
		);
		~NginxVMEnvironment() throw();
		
		void Process(
			VMMemoryCore const  &pVMMemoryCore,
			CDT                 &oHash,
			OutputCollector     &oOutputCollector,
			Logger              &oLogger
		);
	
	private:
		const UINT_32  iStepsLimit;
		const UINT_32  iIMaxHandlers;
		const UINT_32  iIMaxArgStackSize;
		const UINT_32  iIMaxCodeStackSize;
		
		SyscallFactory oSyscallFactory;
		VM *oVM;
};

} // namespace CTPPMODNginx
#endif // _CTPP2_NGINX_VM_ENVIROUNMENT_HPP__ 
