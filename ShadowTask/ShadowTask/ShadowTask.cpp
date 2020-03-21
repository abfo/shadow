// ShadowTask.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

HRESULT WaitForAsync(IVssAsync*	pAsync)
{
	HRESULT hr = pAsync->Wait();

	pAsync->Release();
	pAsync = NULL;

	return hr;
}

int _tmain(int argc, _TCHAR* argv[])
{
	DWORD exitCode = 0;
	bool bOK = true;
	bool snapshotExists = false;
	bool snapshotExposed = false;
	VSS_PWSZ actualExposed = NULL;

	wprintf_s(L"ShadowTask\r\n");
	if (4 != argc)
	{
		wprintf_s(L"Usage: ShadowTask SourceLetter TargetLetter Task\r\n");
		wprintf_s(L"  i/e: ShadowTask C K copy.bat\r\n");
		return -1;
	}

	wprintf_s(L"Creating shadow copy...\r\n");

	wchar_t srcDrive[4];
	srcDrive[0] = argv[1][0];
	srcDrive[1] = ':';
	srcDrive[2] = '\\';
	srcDrive[3] = '\0';

	wchar_t dstDrive[3];
	dstDrive[0] = argv[2][0];
	dstDrive[1] = ':';
	dstDrive[2] = '\0';

	IVssBackupComponents* pVssBackupComponents = NULL;
	HRESULT hr;

	VSS_ID snapshotSetID = GUID_NULL;
	VSS_ID snapshotVolID = GUID_NULL;
	IVssAsync* pAsync = NULL;
	VSS_SNAPSHOT_PROP vsp;

	SecureZeroMemory(&vsp, sizeof(VSS_SNAPSHOT_PROP));

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (!SUCCEEDED(hr))
	{
		wprintf_s(L"Failed to initialize COM :(\r\n");
		bOK = false;
	}

	// create components
	if (bOK)
	{
		hr = CreateVssBackupComponents(&pVssBackupComponents);
		if (!SUCCEEDED(hr))
		{
			wprintf_s(L"Failed to create VSS backup components :(\r\n");
			bOK = false;
			pVssBackupComponents = NULL;
		}
	}

	// init for backup
	if (bOK)
	{
		hr = pVssBackupComponents->InitializeForBackup();
		if (!SUCCEEDED(hr))
		{
			wprintf_s(L"Failed to initialize VSS for backup :(\r\n");
			bOK = false;
		}
	}

	// set context
	if (bOK)
	{
		//VSS_CTX_APP_ROLLBACK
		//VSS_CTX_NAS_ROLLBACK
		hr = pVssBackupComponents->SetContext(VSS_CTX_APP_ROLLBACK);
		if (!SUCCEEDED(hr))
		{
			wprintf_s(L"Failed to set VSS context :(\r\n");
			bOK = false;
		}
	}

	// full backup
	if (bOK)
	{
		hr = pVssBackupComponents->SetBackupState(false, false, VSS_BT_FULL, false);
		if (!SUCCEEDED(hr))
		{
			wprintf_s(L"Failed to set VSS backup state :(\r\n");
			bOK = false;
		}
	}

	// start the snapshot
	if (bOK)
	{
		hr = pVssBackupComponents->StartSnapshotSet(&snapshotSetID);
		if (!SUCCEEDED(hr))
		{
			wprintf_s(L"Failed to start VSS snapshot :(\r\n");
			bOK = false;
		}
	}

	// check to see if the source drive is supported
	if (bOK)
	{
		BOOL bSupported = false;
		
		hr = pVssBackupComponents->IsVolumeSupported(GUID_NULL, srcDrive, &bSupported);
		if (SUCCEEDED(hr))
		{
			if (!bSupported)
			{
				wprintf_s(L"Source drive is not supported for VSS :(\r\n");
				bOK = false;
			}
		}
		else
		{
			wprintf_s(L"Failed to determine if source drive is supported for VSS :(\r\n");
			bOK = false;
		}
	}
	
	// add the source drive
	if (bOK)
	{
		hr = pVssBackupComponents->AddToSnapshotSet(srcDrive, GUID_NULL, &snapshotVolID);
		if (!SUCCEEDED(hr))
		{
			wprintf_s(L"Failed to add source drive to VSS snapshot :(\r\n");
			bOK = false;
		}
	}

	// prepare for backup
	if (bOK)
	{
		hr = pVssBackupComponents->PrepareForBackup(&pAsync);

		if (SUCCEEDED(hr))
		{
			hr = WaitForAsync(pAsync);

			if (!SUCCEEDED(hr))
			{
				wprintf_s(L"Failed to wait for VSS to prepare for backup :(\r\n");
				bOK = false;
			}
		}
		else
		{
			wprintf_s(L"Failed to prepare VSS for backup :(\r\n");
			bOK = false;
		}
	}

	// make the copy
	if (bOK)
	{
		hr = pVssBackupComponents->DoSnapshotSet(&pAsync);

		if (SUCCEEDED(hr))
		{
			hr = WaitForAsync(pAsync);

			if (SUCCEEDED(hr))
			{	
				// we've got a snapshot, need to tell any writers when we're finished with it
				snapshotExists = true;
			}
			else
			{
				wprintf_s(L"Failed to wait for VSS to take a snapshot :(\r\n");
				bOK = false;
			}
		}
		else
		{
			wprintf_s(L"Failed to create a VSS snapshot:(\r\n");
			bOK = false;
		}
	}

	// expose the snapshot
	if (bOK)
	{
		hr = pVssBackupComponents->ExposeSnapshot(snapshotVolID, NULL, VSS_VOLSNAP_ATTR_EXPOSED_LOCALLY, dstDrive, &actualExposed);

		if (SUCCEEDED(hr))
		{
			if (actualExposed != NULL)
			{
				snapshotExposed = true;
			}
			else
			{
				wprintf_s(L"Failed to expose the VSS snapshot:(\r\n");
				bOK = false;
			}
		}
		else
		{
			wprintf_s(L"Failed to expose the VSS snapshot:(\r\n");
			bOK = false;
		}
	}

	// run the target app
	if (snapshotExposed)
	{
		wprintf_s(L"Running Task...\r\n");

		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		
		GetStartupInfo(&si);
		ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

		if (CreateProcess(NULL,
				argv[3],
				NULL,
				NULL,
				FALSE,
				0,
				NULL,
				NULL,
				&si,
				&pi))
		{
			// wait for the target to complete
			WaitForSingleObject(pi.hProcess, INFINITE);

			// get the exit code
			GetExitCodeThread(pi.hThread, &exitCode);

			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
		}
		else
		{
			wprintf_s(L"Failed to run task :(\r\n");
		}
	}

	if (snapshotExists)
	{
		wprintf_s(L"Removing shadow copy...\r\n");

		LONG deleted;
		VSS_ID firstNotDeleted;
		hr = pVssBackupComponents->DeleteSnapshots(snapshotSetID, VSS_OBJECT_SNAPSHOT_SET, FALSE, &deleted, &firstNotDeleted);
		if (!SUCCEEDED(hr))
		{
			wprintf_s(L"Failed to remove snapshot - MANUAL CLEANUP MAP BE REQUIRED :(\r\n");
			bOK = false;
		}
	}
	
	// release the snapshot
	if (NULL != pVssBackupComponents)
	{
		pVssBackupComponents->Release();
		pVssBackupComponents = NULL;
	}

	// free the system string (has caused heap corruption so try to avoid screwing it up...
	/*if (actualExposed != NULL)
	{
		SysFreeString(actualExposed);
	}*/

	CoUninitialize();
	return exitCode;
}

