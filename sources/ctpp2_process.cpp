
/*
 * Copyright (C) Valentin V. Bartenev
 */


#include "ctpp2_process.h"

#include <ctpp2/CTPP2Util.hpp>
#include <ctpp2/CTPP2JSONParser.hpp>
#include <ctpp2/CTPP2OutputCollector.hpp>
#include <ctpp2/CTPP2Logger.hpp>
#include <ctpp2/CTPP2VMDebugInfo.hpp>
#include <ctpp2/CTPP2VMStackException.hpp>
#include <ctpp2/CTPP2VMExecutable.hpp>
#include <ctpp2/CTPP2VMMemoryCore.hpp>

#include "CTPP2NginxVMEnvironment.hpp"


using namespace CTPP;
using namespace CTPPNginx;

static NginxVMEnvironment *oNginxVMEnvironment = NULL;

class NginxOutputCollector : public OutputCollector {
	public:
		NginxOutputCollector(ngx_pool_t *pool, ngx_chain_t *out) throw() :
			nginxPool(pool), nginxOutput(out), total(0) { ;; }
		~NginxOutputCollector() throw() { nginxOutput->next = NULL; }
		
		size_t getSize() const throw() { return total; }

	private:
		ngx_pool_t   *nginxPool;
		ngx_chain_t  *nginxOutput;
		size_t        total;
		
		INT_32 Collect(const void *vData, UINT_32 iDataLength) /*throw(ngx_int_t)*/;
};

class NginxLogger : public Logger {
	public:
		NginxLogger(ngx_log_t  *log) throw() : Log(log)
		{
			SetPriority(revTrans(log->log_level));
		}
		~NginxLogger() throw() { ;; }

		INT_32 WriteLog(const UINT_32 iPriority, CCHAR_P szString, const UINT_32  iStringLen) throw()
		{
			ngx_log_error(Trans(iPriority), Log, 0, "ctpp2 log message: \"%s\" received", szString);
			return 0;
		}

	private:
		ngx_log_t  *Log;
		
		static ngx_uint_t Trans(const UINT_32 iPriority) throw() 
		{
			switch(iPriority) {
				case CTPP2_LOG_EMERG  : return NGX_LOG_EMERG;
				case CTPP2_LOG_ALERT  : return NGX_LOG_ALERT;
				case CTPP2_LOG_CRIT   : return NGX_LOG_CRIT;
				case CTPP2_LOG_ERR    : return NGX_LOG_ERR;
				case CTPP2_LOG_WARN   : return NGX_LOG_WARN;
				case CTPP2_LOG_NOTICE : return NGX_LOG_NOTICE;
				case CTPP2_LOG_INFO   : return NGX_LOG_INFO;
				case CTPP2_LOG_DEBUG  : return NGX_LOG_DEBUG;
			}
			return NGX_LOG_DEBUG;
		}
		static UINT_32 revTrans(const ngx_uint_t level) throw() 
		{
			switch(level) {
				case NGX_LOG_EMERG  : return CTPP2_LOG_EMERG;
				case NGX_LOG_ALERT  : return CTPP2_LOG_ALERT;
				case NGX_LOG_CRIT   : return CTPP2_LOG_CRIT;
				case NGX_LOG_ERR    : return CTPP2_LOG_ERR;
				case NGX_LOG_WARN   : return CTPP2_LOG_WARN;
				case NGX_LOG_NOTICE : return CTPP2_LOG_NOTICE;
				case NGX_LOG_INFO   : return CTPP2_LOG_INFO;
				case NGX_LOG_DEBUG  : return CTPP2_LOG_DEBUG;
			}
			return CTPP2_LOG_DEBUG;
		}
};


ngx_int_t
ctpp2_init(ngx_uint_t args, ngx_uint_t code, ngx_uint_t funcs, ngx_uint_t steps)
{
	if (oNginxVMEnvironment == NULL) {
		try {
			oNginxVMEnvironment = new NginxVMEnvironment(steps, funcs, args, code);
		}
		catch(...) {
			return NGX_ERROR;
		}
	}
	
	return NGX_OK;
}


ngx_int_t
ctpp2_tmpltest(ngx_buf_t *tmpl, ngx_flag_t check, ngx_log_t *log)
{
	VMExecutable *oCore = (VMExecutable *) tmpl->pos;
	
	if (oCore->magic[0] == 'C' &&
	    oCore->magic[1] == 'T' &&
	    oCore->magic[2] == 'P' &&
	    oCore->magic[3] == 'P')
	{
		if (oCore->version[0] >= 1) {
			if (oCore->platform == 0x4142434445464748ull) {
				if (check) {
					UINT_32 Size = tmpl->last - tmpl->pos;
					UINT_32 iCRC = oCore->crc;
					oCore->crc = 0;
					if (iCRC != crc32((UCCHAR_P) oCore, Size)) {
						ngx_log_error(NGX_LOG_ERR, log, 0,
							"CTPP2 template test: CRC checksum invalid");
						return NGX_ERROR;
					}
				}
			} else {
				ngx_log_error(NGX_LOG_ERR, log, 0,
					"CTPP2 template test: wrong byte-order; template has been compiled on a different platform");
				return NGX_ERROR;
			}
			
			if (oCore->ieee754double != 15839800103804824402926068484019465486336.0) {
				ngx_log_error(NGX_LOG_ERR, log, 0,
					"CTPP2 template test: IEEE 754 format is broken");
				return NGX_ERROR;
			}
		}
		
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "ctpp2 template test - OK");
		return NGX_OK;
	}
	
	ngx_log_error(NGX_LOG_ERR, log, 0,
		"CTPP2 template test: it doesn't look like compiled template");
	return NGX_ERROR;
}


ngx_int_t
ctpp2_process(
	ngx_buf_t     *tmpl,
	ngx_buf_t     *data,
	ngx_pool_t    *pool,
	ngx_chain_t  **out,
	size_t        *out_size,
	ngx_log_t     *log
)
{
	try {
		CDT oHash(CDT::HASH_VAL);
		CTPP2JSONParser oJSONParser(oHash);
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "ctpp2 template processing (1/3): some inits - DONE");
		
		oJSONParser.Parse((char *) data->pos, (char *) data->last);
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "ctpp2 template processing (2/3): parsing json data - DONE");
		
		const VMMemoryCore pVMMemoryCore((VMExecutable *) tmpl->pos);
		
		ngx_chain_t *chain = ngx_alloc_chain_link(pool);
		if (chain == NULL) throw NGX_ERROR;
		
		data->last = data->start;
		chain->buf = data;
		
		NginxOutputCollector oOutputCollector(pool, chain);
		NginxLogger oLogger(log);
		
		oNginxVMEnvironment->Process(pVMMemoryCore, oHash, oOutputCollector, oLogger);
		ngx_log_debug0(NGX_LOG_DEBUG_HTTP, log, 0, "ctpp2 template processing (3/3): VM executing - DONE");
		
		if (data->last - data->start) {
			*out = chain;
			*out_size = oOutputCollector.getSize();
		} else {
			*out = NULL;
			*out_size = 0;
			chain->buf = NULL;
			ngx_free_chain(pool, chain);
			ngx_pfree(pool, data->start);
		}
		
		return NGX_DONE;
	}
	// CDT
	catch(CDTTypeCastException  & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"CDT error: Type Cast %s", e.what());
	}
	catch(CDTAccessException    & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"CDT error: Array index out of bounds: %s", e.what());
	}

	// Virtual machine
	catch(IllegalOpcode         & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"VM error: Illegal opcode 0x%08XD at 0x%08XD", e.GetOpcode(), e.GetIP());
	}
	catch(InvalidSyscall        & e) { 
		if (e.GetIP() != 0) {
			VMDebugInfo oVMDebugInfo(e.GetDebugInfo());
			ngx_log_error(
				NGX_LOG_ERR, log, 0,
				"VM error: %s at 0x%08XD (Template file \"%s\", Line %D, Pos %D)",
				e.what(), e.GetIP(), e.GetSourceName(), oVMDebugInfo.GetLine(), oVMDebugInfo.GetLinePos()
			);
		} else {
			ngx_log_error(NGX_LOG_ERR, log, 0,
				"VM error: Unsupported syscall \"%s\"", e.what());
		}
	}
	catch(InvalidCall           & e) {
		VMDebugInfo oVMDebugInfo(e.GetDebugInfo());
		ngx_log_error(
			NGX_LOG_ERR, log, 0,
			"VM error at 0x%08X: Invalid block name \"%s\" in file \"%s\", Line %D, Pos %D",
			e.GetIP(), e.what(), e.GetSourceName(), oVMDebugInfo.GetLine(), oVMDebugInfo.GetLinePos()
		);
	}
	catch(CodeSegmentOverrun    & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"VM error: %s at 0x%08XD", e.what(),  e.GetIP());
	}
	catch(StackOverflow         & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"VM error: Stack overflow at 0x%08XD", e.GetIP());
	}
	catch(StackUnderflow        & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"VM error: Stack underflow at 0x%08XD", e.GetIP());
	}
	catch(ExecutionLimitReached & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"VM error: Execution limit of steps reached at 0x%08XD", e.GetIP());
	}
	catch(VMException           & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"VM generic exception: %s at 0x%08XD", e.what(), e.GetIP());
	}

	// CTPP
	catch(CTPPLogicError        & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"CTPP error: %s", e.what());
	}
	catch(CTPPUnixException     & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"CTPP I/O error in %s: %s", e.what(), strerror(e.ErrNo())); 
	}
	catch(CTPPException         & e) { 
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"CTPP generic exception: %s", e.what());
	}
	
	// Nginx
	catch(ngx_int_t  & rc) { return rc; }
	catch(...) {
		ngx_log_error(NGX_LOG_ERR, log, 0,
			"NginxCTPP module error: Unknown exception catched");
	}

	return NGX_ERROR;
}


INT_32
NginxOutputCollector::Collect(const void *vData, UINT_32 iDataLength) /*throw(ngx_int_t)*/
{
	ngx_buf_t    *buffer;
	size_t        freeSpace;
	UINT_32       size;
	u_char       *charData;
	ngx_chain_t  *chain;
	
	charData = (u_char *) vData;
	total += iDataLength;
	buffer = nginxOutput->buf;
	freeSpace = buffer->end - buffer->last;
	
	do {
		size = (freeSpace > iDataLength) ? iDataLength : freeSpace;
		buffer->last = ngx_cpymem(buffer->last, charData, size);
		iDataLength -= size;
		if (!iDataLength) return 0;
		
		charData += size;
		
		freeSpace = ngx_pagesize;
		buffer = ngx_create_temp_buf(nginxPool, freeSpace);
		if (buffer == NULL) throw NGX_ERROR;
		
		chain = ngx_alloc_chain_link(nginxPool);
		if (chain == NULL) throw NGX_ERROR;
		chain->buf = buffer;
		
		nginxOutput->next = chain;
		nginxOutput = chain;
	} while (true);
}
