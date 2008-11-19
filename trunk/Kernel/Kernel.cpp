/* This file is part of the FaCT++ DL reasoner
Copyright (C) 2003-2008 by Dmitry Tsarkov

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "Kernel.h"
#include "comerror.h"

const char* ReasoningKernel :: Version = "1.2.0";
const char* ReasoningKernel :: ProductName =
	"FaCT++.Kernel: Reasoner for the SROIQ(D) Description Logic\n";
const char* ReasoningKernel :: Copyright =
	"Copyright (C) Dmitry V. Tsarkov, 2002-2008. ";
const char* ReasoningKernel :: ReleaseDate = "(25 September 2008)";

// print the FaCT++ information only once
static bool KernelFirstRun = true;

ReasoningKernel :: ReasoningKernel ( void )
	: pKernelOptions (NULL)
	, pTBox (NULL)
	, cachedQuery(NULL)
{
	// Intro
	if ( KernelFirstRun )
	{
		std::cerr << ProductName << Copyright << "Version " << Version << " " << ReleaseDate << "\n";
		KernelFirstRun = false;
	}

	initCacheAndFlags();

	// init option set (fill with options):
	if ( initOptions () )
		error ( "Cannot init options" );
}

void
ReasoningKernel :: processKB ( KBStatus status )
{
	assert ( status >= kbCChecked );

	// check whether reasoning was failed
	if ( reasoningFailed )
		throw EFaCTPlusPlus("Can't classify KB because of previous errors");

	// check if something have to be done
	if ( getStatus() >= status )
	{	// nothing to do; but make sure that we are consistent
		if ( !isKBConsistent() )
			throw InconsistentKB();
		return;
	}

	// here we have to do something: let's decide what to do
	switch ( getStatus() )
	{
	case kbLoading:		break;	// need to do the whole cycle -- just after the switch
	case kbCChecked:	goto Classify;	// do classification
	case kbClassified:	goto Realise;	// do realisation
	default:	// nothing should be here
		assert(0);
	}

	// start with loading and preprocessing -- here might be a failures
	reasoningFailed = true;

#ifdef FPP_USE_AXIOMS
	// load the axioms from the ontology
	Ontology.load(*pTBox);
#endif

	// forbid further changes
	isChanged = false;

	// do the consistency check
	pTBox->isConsistent();

	// if there were no exception thrown -- clear the failure status
	reasoningFailed = false;

	if ( status == kbCChecked )
		return;

Classify:	// do classification

	// don't do classification twice
	if ( status == kbRealised )
		goto Realise;

	if ( !pTBox->isConsistent() )
		return;

	pTBox->performClassification();
	return;

Realise:	// do realisation

	if ( !pTBox->isConsistent() )
		return;

	pTBox->performRealisation();
}

//******************************************
//* caching support
//******************************************
void
ReasoningKernel :: setUpCache ( DLTree* query, cacheStatus level )
{
	// check if the query is already cached
	if ( isCached (query) )
	{	// ... with the same level -- nothing to do
		if ( level <= cacheLevel )
			return;
		else
		{	// concept was defined but not classified yet
			assert ( level == csClassified && cacheLevel != csClassified );
			if ( cacheLevel == csEmpty )
				goto needSetup;
			else
				goto needClassify;
		}
	}

	// if KB was changed since it was classified -- error (inclremental classification is not supported)
	if ( isChanged )
	{
		// invalidate cache
		cacheLevel = csEmpty;
		throw EFaCTPlusPlus("FaCT++ Kernel: incremental classification not supported");
	}

	// change current query
	deleteTree(cachedQuery);
	cachedQuery = clone (query);

needSetup:
	// clean cached info
	cachedVertex = NULL;

	// check if concept-to-cache is defined in ontology
	if ( isCN (query) )
	{
		cachedConcept = getTBox()->getCI(query);

		// undefined/non-classified concept -- need to reclassify
		if ( cachedConcept == NULL )
		{
			// invalidate cache
			cacheLevel = csEmpty;
			// FIXME!! reclassification
			throw EFaCTPlusPlus("FaCT++ Kernel: incremental classification not supported");
		}

		cacheLevel = level;

		if ( level == csClassified )	// need to set the pointers
		{
			classifyKB();
			cachedVertex = cachedConcept->getTaxVertex();
		}
		return;
	}

	// we are preprocessed here

	// case of complex query
	cachedConcept = getTBox()->createTempConcept(query);
	cacheLevel = level;

needClassify:	// classification only needed for complex expression

	if ( level == csClassified )
	{
		classifyKB();
		getTBox()->classifyTempConcept();
		// cached concept now have to be classified
		assert ( cachedConcept->isClassified() );
		cachedVertex = cachedConcept->getTaxVertex();
	}
}

//******************************************
//* Initialization
//******************************************

bool ReasoningKernel :: initOptions ( void )
{
	// register all possible options used in FaCT++ Kernel
	assert ( pKernelOptions == NULL );
	pKernelOptions = new ifOptionSet;

	// options for TBox

	// register "useRelevantOnly" option
	if ( pKernelOptions->RegisterOption (
		"useRelevantOnly",
		"Option 'useRelevantOnly' is used when creating internal DAG representation for externally given TBox. "
		"If true, DAG contains only concepts, relevant to query. It is safe to leave this option false.",
		ifOption::iotBool,
		"false"
		) )
		return true;

	// register "useRangeDomain" option
	if ( pKernelOptions->RegisterOption (
		"useRangeDomain",
		"Option 'useRangeDomain' switch on and off native support for the range and domain. "
		"This option is of internal use only. It is crusial for reasoning performance to leave this option true.",
		ifOption::iotBool,
		"true"
		) )
		return true;

	// register "absorptionFlags" option (04/05/2005)
	if ( pKernelOptions->RegisterOption (
		"absorptionFlags",
		"Option 'absorptionFlags' sets up absorption process for general axioms. "
		"It is 5-letters text field; "
		"1st symbol -- 'n' or 'c' -- (doN't) use Concept absorption; "
		"2nd symbol -- 'n', 'r' or 'e' -- (doN't) use (Extended) Role absorption; "
		"the rest is symbols C,R,S; their order setup order of "
		"Simplification, Concept and Role absorptions performed (if possible).",
		ifOption::iotText,
		"ceSCR"
		) )
		return true;

	// register "alwaysPreferEquals" option (26/01/2006)
	if ( pKernelOptions->RegisterOption (
		"alwaysPreferEquals",
		"Option 'alwaysPreferEquals' allows user to enforce usage of C=D definition instead of C[=D "
		"during absorption, even if implication appeares earlier in stream of axioms.",
		ifOption::iotBool,
		"true"
		) )
		return true;

	// register "usePrecompletion" option (13/09/2007)
	if ( pKernelOptions->RegisterOption (
		"usePrecompletion",
		"Option 'usePrecompletion' switchs on and off precompletion process for ABox.",
		ifOption::iotBool,
		"false"
		) )
		return true;

	// options for DLDag

	// register "orSortSub" option (20/12/2004)
	if ( pKernelOptions->RegisterOption (
		"orSortSub",
		"Option 'orSortSub' define the sorting order of OR vertices in the DAG used in subsumption tests. "
		"Option has form of string 'Mop', where 'M' is a sort field (could be 'D' for depth, 'S' for size, 'F' "
		"for frequency, and '0' for no sorting), 'o' is a order field (could be 'a' for ascending and 'd' "
		"for descending mode), and 'p' is a preference field (could be 'p' for preferencing non-generating "
		"rules and 'n' for not doing so).",
		ifOption::iotText,
		"0"
		) )
		return true;

	// register "orSortSat" option (20/12/2004)
	if ( pKernelOptions->RegisterOption (
		"orSortSat",
		"Option 'orSortSat' define the sorting order of OR vertices in the DAG used in satisfiability tests "
		"(used mostly in caching). Option has form of string 'Mop', see orSortSub for details.",
		ifOption::iotText,
		"0"
		) )
		return true;

	// options for ToDoTable

	// register "IAOEFLG" option
	if ( pKernelOptions->RegisterOption (
		"IAOEFLG",
		"Option 'IAOEFLG' define the priorities of different operations in TODO list. Possible values are "
		"7-digit strings with ony possible digit are 0-6. The digits on the places 1, 2, ..., 7 are for "
		"priority of Id, And, Or, Exists, Forall, LE and GE operations respectively. The smaller number means "
		"the higher priority. All other constructions (TOP, BOTTOM, etc) has priority 0.",
		ifOption::iotText,
		"1263005"
		) )
		return true;

	// options for Reasoner

	// register "useSemanticBranching" option
	if ( pKernelOptions->RegisterOption (
		"useSemanticBranching",
		"Option 'useSemanticBranching' switch semantic branching on and off. The usage of semantic branching "
		"usually leads to faster reasoning, but sometime could give small overhead.",
		ifOption::iotBool,
		"true"
		) )
		return true;

	// register "useBackjumping" option
	if ( pKernelOptions->RegisterOption (
		"useBackjumping",
		"Option 'useBackjumping' switch backjumping on and off. The usage of backjumping "
		"usually leads to much faster reasoning.",
		ifOption::iotBool,
		"true"
		) )
		return true;

	// register "useLazyBlocking" option -- 08-03-04
	if ( pKernelOptions->RegisterOption (
		"useLazyBlocking",
		"Option 'useLazyBlocking' makes checking of blocking status as small as possible. This greatly "
		"increase speed of reasoning.",
		ifOption::iotBool,
		"true"
		) )
		return true;

	// register "useAnywhereBlocking" option (18/08/2008)
	if ( pKernelOptions->RegisterOption (
		"useAnywhereBlocking",
		"Option 'useAnywhereBlocking' allow user to choose between Anywhere and Ancestor blocking.",
		ifOption::iotBool,
		"true"
		) )
		return true;

	// register "dumpQuery" option -- 11-08-04
	if ( pKernelOptions->RegisterOption (
		"dumpQuery",
		"Option 'dumpQuery' dumps sub-TBox relevant to given satisfiability/subsumption query.",
		ifOption::iotBool,
		"false"
		) )
		return true;

	// options for Taxonomy

	// register "useCompletelyDefined" option
	if ( pKernelOptions->RegisterOption (
		"useCompletelyDefined",
		"Option 'useCompletelyDefined' leads to simpler Taxonomy creation if TBox contains no non-primitive "
		"concepts. Unfortunately, it is quite rare case.",
		ifOption::iotBool,
		"true"
		) )
		return true;

	// all was registered OK
	return false;
}

