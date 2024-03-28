// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file  EditPluginDlg.cpp
 *
 * @brief Unpacker plugin selection dialog implementation.
 */

#include "stdafx.h"
#include "EditPluginDlg.h"
#include "Plugins.h"
#include "FileTransform.h"
#include "OptionsMgr.h"
#include "OptionsDef.h"
#include "unicoder.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

/////////////////////////////////////////////////////////////////////////////
// CEditPluginDlg dialog

void CEditPluginDlg::Initialize(PluginType pluginType)
{
	//{{AFX_DATA_INIT(CEditPluginDlg)
	m_strDescription.clear();
	m_strExtensions.clear();
	m_strArguments.clear();
	//}}AFX_DATA_INIT
}

CEditPluginDlg::CEditPluginDlg(internal_plugin::Info& info, CWnd* pParent/* = nullptr*/)
	: CTrDialog(CEditPluginDlg::IDD, pParent)
	, m_info(info)
	, m_strEvent(info.m_event)
	, m_strPluginName(info.m_name)
	, m_strDescription(info.m_description)
	, m_strExtensions(info.m_fileFilters)
	, m_strArguments(info.m_arguments)
	, m_strPluginPipeline(info.m_pipeline)
{
}

CEditPluginDlg::~CEditPluginDlg()
{
}

void CEditPluginDlg::DoDataExchange(CDataExchange* pDX)
{
	CTrDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CEditPluginDlg)
	DDX_Control(pDX, IDC_PLUGIN_TYPE, m_ctlEvent);
	DDX_Text(pDX, IDC_PLUGIN_NAME, m_strPluginName);
	DDX_Text(pDX, IDC_PLUGIN_DESCRIPTION, m_strDescription);
	DDX_Text(pDX, IDC_PLUGIN_SUPPORTED_EXTENSIONS, m_strExtensions);
	DDX_Text(pDX, IDC_PLUGIN_ARGUMENTS, m_strArguments);
	DDX_Control(pDX, IDC_PLUGIN_PIPELINE, m_ctlPluginPipeline);
	DDX_CBStringExact(pDX, IDC_PLUGIN_PIPELINE, m_strPluginPipeline);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CEditPluginDlg, CTrDialog)
	//{{AFX_MSG_MAP(CEditPluginDlg)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CEditPluginDlg message handlers

void CEditPluginDlg::OnOK()
{
	UpdateData(TRUE);

	m_info.m_event = reinterpret_cast<wchar_t*>(m_ctlEvent.GetItemDataPtr(m_ctlEvent.GetCurSel()));
	m_info.m_name = m_strPluginName;
	m_info.m_description = m_strDescription;
	m_info.m_fileFilters = m_strExtensions;
	m_info.m_arguments = m_strArguments;
	m_info.m_pipeline = m_strPluginPipeline;

	CTrDialog::OnOK();
}

BOOL CEditPluginDlg::OnInitDialog()
{
	CTrDialog::OnInitDialog();

	// setup handler for resizing this dialog	
	m_constraint.InitializeCurrentSize(this);
	m_constraint.SubclassWnd(); // install subclassing
	// persist size via registry
	m_constraint.LoadPosition(_T("ResizeableDialogs"), _T("EditPluginDlg"), false);

	SetDlgItemComboBoxList(IDC_PLUGIN_TYPE,
		{ _("URL Handler"), _("Unpacker"), _("Prediffer"), _("Unpacker alias"), _("Prediffer alias"), _("Editor script alias") });
	int i = 0;
	for (auto* event : { L"URL_PACK_UNPACK", L"FILE_PACK_UNPACK", L"PREDIFF_UNPACK", L"ALIAS_PACK_UNPACK", L"ALIAS_PRDIFF", L"ALIAS_EDIT_SCRIPT" })
		m_ctlEvent.SetItemDataPtr(i++, const_cast<wchar_t*>(event));
	const int count = m_ctlEvent.GetCount();
	for (i = 0; i < count; i++)
	{
		if (m_strEvent == reinterpret_cast<wchar_t*>(m_ctlEvent.GetItemDataPtr(i)))
			m_ctlEvent.SetCurSel(i);
	}

	m_ctlPluginPipeline.SetFileControlStates(true);
	m_ctlPluginPipeline.LoadState(
		std::vector<const tchar_t *>{_T("Files\\Unpacker"), _T("Files\\Prediffer"), _T("Files\\EditorScript") }
			[static_cast<int>(m_pluginType)]);


	UpdateData(FALSE);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

