/*
 * Project:		TinyCAD program for schematic capture
 *				http://tinycad.sourceforge.net
 * Copyright:	� 1994-2005 Matt Pyne
 * License:		Lesser GNU Public License 2.1 (LGPL)
 *				http://www.opensource.org/licenses/lgpl-license.html
 */

#include "stdafx.h"
#include "resource.h"
#include "DlgAbout.h"
#include "TinyCad.h"

//*************************************************************************
//*                                                                       *
//* Shows information of the program like name of the programmer e.g.     *
//*                                                                       *
//*************************************************************************

//=========================================================================
//== ctor/dtor/initializing                                              ==
//=========================================================================

//-------------------------------------------------------------------------
CDlgAbout::CDlgAbout()
: super( IDD_ABOUTBOX )
{
}
//-------------------------------------------------------------------------
BOOL CDlgAbout::OnInitDialog()
{
	super::OnInitDialog();

	CString sVersion = CTinyCadApp::GetName() + " " + CTinyCadApp::GetVersion();

	GetDlgItem( IDC_VERSION )->SetWindowText( sVersion );

	return TRUE;
}
//-------------------------------------------------------------------------
