
/*
 * Copyright (C) Valentin V. Bartenev
 */


#include "CTPP2NginxVMEnvironment.hpp"
#include <ctpp2/CTPP2VMSTDLib.hpp>

using namespace CTPP;

namespace CTPPNginx { // CT++ Module for Nginx

NginxVMEnvironment::NginxVMEnvironment(
		const UINT_32  iStepsLimit,
		const UINT_32  iIMaxHandlers,
		const UINT_32  iIMaxArgStackSize,
		const UINT_32  iIMaxCodeStackSize
	):
		iStepsLimit(iStepsLimit),
		iIMaxHandlers(iIMaxHandlers),
		iIMaxArgStackSize(iIMaxArgStackSize),
		iIMaxCodeStackSize(iIMaxCodeStackSize),
		oSyscallFactory(iIMaxHandlers)
{
	STDLibInitializer::InitLibrary(oSyscallFactory);
	oVM = new VM(&oSyscallFactory, iIMaxArgStackSize, iIMaxCodeStackSize, iStepsLimit);
}

NginxVMEnvironment::~NginxVMEnvironment() throw()
{
	delete oVM;
	STDLibInitializer::DestroyLibrary(oSyscallFactory);
}

void NginxVMEnvironment::Process(
		VMMemoryCore const  &pVMMemoryCore,
		CDT                 &oHash,
		OutputCollector     &oOutputCollector,
		Logger              &oLogger
	)
{
	UINT_32 iIP = 0;
	
	try {
		oVM->Init(&pVMMemoryCore, &oOutputCollector, &oLogger);
		oVM->Run(&pVMMemoryCore, &oOutputCollector, iIP, oHash, &oLogger);
	}
	catch(...) {
		oVM->Reset();
		throw;
	}
	
	oVM->Reset();
}

} // namespace CTPPNginx 
