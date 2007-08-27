/******************************************************************************

GHAAS Water Balance/Transport Model V3.0
Global Hydrologic Archive and Analysis System
Copyright 1994-2007, University of New Hampshire

MDIrrigation.c

dominik.wisser@unh.edu

*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cm.h>
#include <MF.h>
#include <MD.h>
#include <math.h>
#define NumStages 4
#define numSeasons 3
#define MDParIrrigationCropFileName "CropParameterFileName"
 
typedef struct {
    int ID;
    int DW_ID;
    char cropName[80];
    char cropDistrFileName[80];
    int cropIsRice;
    float cropSeasLength[NumStages];
    float cropKc[NumStages-1];
    float cropRootingDepth;
    float cropDepletionFactor;
    float cropLeachReq;
} MDIrrigatedCrop;


static MDIrrigatedCrop *_MDirrigCropStruct = (MDIrrigatedCrop *) NULL;
static int *_MDInCropFractionID = (int *) NULL;
static int *_MDOutCropDeficitID = (int *) NULL;

//Input
static int _MDInPrecipID               = MFUnset;
static int _MDInRefETPID               = MFUnset;
static int _MDInIrrAreaID              = MFUnset;
static int _MDInIrrIntensityID         = MFUnset;
static int _MDInIrrEfficiencyID        = MFUnset;
static int _MDInSPackChgID             = MFUnset;
static int _MDGrowingSeason1ID         = MFUnset;
static int _MDGrowingSeason2ID         = MFUnset;
static int _MDGrowingSeason3ID         = MFUnset;
static int _MDInFldCapaID              = MFUnset;
static int _MDInWltPntID               = MFUnset;
static int _MDNumberOfIrrCrops         = MFUnset;
 

static int _MDGrowingSeasonStartCalcID = MFUnset;
static int _MDGrowingSeasonEndCalcID   = MFUnset;
static int _MDIrrConstantKc            = MFUnset;
//Output


// The following Outputs are given in mm/d and related to the total cell area!! Module is called from MDRunoff!
static int _MDOutNetIrrDemandID        = MFUnset;
static int _MDOutIrrigationDrainageID  = MFUnset;
static int _MDOutTotalCropETPDepthID   = MFUnset;
static int _MDOutGrossIrrDemandID      = MFUnset;
static int _MDOutIrrAreaSMChangeID     = MFUnset;
static int _MDInAirTemperatureID       = MFUnset;


//Parameters
static float _MDParIrrDailyPercolation;


static const char *CropParameterFileName;

static int   getTotalSeasonLength(const MDIrrigatedCrop *);
static int   getDaysSincePlanting(int, int [],int,const MDIrrigatedCrop *);
static int   getCropStage(const MDIrrigatedCrop *, int);
static float getCropKc(const MDIrrigatedCrop *, int, int);
static float getCurCropRootingDepth(MDIrrigatedCrop *, int);
static float getEffectivePrecipitation(float);
static float getCorrDeplFactor(const MDIrrigatedCrop *, float);
static int   readCropParameters(const char *);
static void  printCrops(const MDIrrigatedCrop *);
static int   getNumGrowingSeasons(float);

static float getIrrGrossWaterDemand(float, float);
static float irrAreaFraction;

static void _MDIrrigationWater(int itemID) {
	int i;
	float ReqpondingDepth=130;
	float pondingDepth;
 	//Output:
	float totalGrossIrrDemand=0;
	float totalNetIrrDemand=0;
	float totalIrrPercolation=0;
	// float totalCropETP=0;
	//Input
	float irrEffeciency;
	float dailyPrecip;
	float dailyEffPrecip;
	float refETP;
	float cropFraction[_MDNumberOfIrrCrops+1];
	float snowpackChg;
 
	if (MFVarTestMissingVal(_MDInRefETPID,  itemID) ||
	    MFVarTestMissingVal(_MDInIrrAreaID, itemID)|| 
	    MFVarTestMissingVal(_MDInIrrEfficiencyID,itemID)||
	    MFVarTestMissingVal(_MDInPrecipID,itemID)||
	   
	    MFVarTestMissingVal(_MDGrowingSeason1ID,itemID)|| 
	    MFVarTestMissingVal(_MDGrowingSeason2ID,itemID)|| 
	    MFVarTestMissingVal(_MDGrowingSeason3ID,itemID)||
	    MFVarTestMissingVal(_MDInWltPntID,  itemID)||
	    MFVarTestMissingVal(_MDInFldCapaID, itemID)||
	    MFVarTestMissingVal(_MDInIrrIntensityID, itemID)) {	
		MFVarSetFloat(_MDOutGrossIrrDemandID,itemID,0.0);
		return;
	}
	    
	for (i=0;i<  _MDNumberOfIrrCrops+1; i++){cropFraction[i]=0;}
	for (i = 0; i < _MDNumberOfIrrCrops; i++) {
	if (MFVarTestMissingVal(_MDInCropFractionID[i],itemID)){MFVarSetFloat(_MDInCropFractionID[i],itemID,0.0);}
	}
	
	irrEffeciency   = MFVarGetFloat(_MDInIrrEfficiencyID, itemID, 0.0);
	dailyPrecip     = MFVarGetFloat(_MDInPrecipID,        itemID, 0.0);
	refETP          = MFVarGetFloat(_MDInRefETPID,        itemID, 0.0);  
	irrAreaFraction = MFVarGetFloat(_MDInIrrAreaID,       itemID, 0.0);
	
	snowpackChg=MFVarGetFloat(_MDInSPackChgID,itemID,0.0); // SPackChg > 0 Snow Acc; <0 : Melting
	int seasStart[3];
	if (snowpackChg >= 0) dailyPrecip=	0; //Snow Accumulation, no liquid precipitation
	if (snowpackChg < 0) dailyPrecip+=fabs(snowpackChg); //Add Snowmelt
 
	
	dailyEffPrecip = getEffectivePrecipitation(dailyPrecip);
	seasStart[0]= MFVarGetFloat(_MDGrowingSeason1ID,      itemID, 100); 
	seasStart[1]= MFVarGetFloat(_MDGrowingSeason2ID,      itemID, 220); 
	seasStart[2]= MFVarGetFloat(_MDGrowingSeason3ID,      itemID, 280); 

	if (irrAreaFraction > 0.0) {
	 	float dailyPercolation    = _MDParIrrDailyPercolation;
	 	float wltPnt              = MFVarGetFloat (_MDInWltPntID,  itemID, 0.0);
		float fldCap              = MFVarGetFloat (_MDInFldCapaID, itemID, 0.0);
		if (fldCap == 0.0) {
			fldCap=0.35;
			wltPnt=0.2;
		}
		float cropDepletionFactor;
		float meanSMChange;
		float irrIntensity        = MFVarGetFloat (_MDInIrrIntensityID, itemID, 0.0) / 100.0;
		if (irrIntensity < 1.5 && irrIntensity > 1.0) irrIntensity = 1.0;
		if (irrIntensity < 2.5 && irrIntensity > 2.0) irrIntensity = 2.0;
		//printf ("Intens %f\n", irrIntensity); 
 		int daysSincePlanted;
		float totAvlWater,readAvlWater, curDepl, prevSoilMstDepl;
		float cropWR, totalCropETP;
		float sumOfCropFractions=0;
		float cropCoeff;
		int stage;
		int curDay = MFDateGetDayOfYear();
		
		float smChange;
		sumOfCropFractions=0;
		for (i = 0; i < _MDNumberOfIrrCrops; i++) {sumOfCropFractions += MFVarGetFloat(_MDInCropFractionID[i],itemID, 0.0);	}
		if (sumOfCropFractions==0) { // No Cropdata for irrigated cell: default to some cereal crop
			MFVarSetFloat(_MDInCropFractionID[1],itemID, 0.3);
		}
		sumOfCropFractions=0;
	  	for (i = 0; i < _MDNumberOfIrrCrops; i++) {sumOfCropFractions += MFVarGetFloat(_MDInCropFractionID[i],itemID, 0.0);	}
	 
		float debugSum;
		float relCropFraction=0;
		float netIrrDemand    = 0;
 
		float deepPercolation = 0;
		meanSMChange=0;totalCropETP=0;
		int   numGrowingSeasons;
		float addBareSoil;
		float curCropFraction;
		// 
		for (i = 0; i < _MDNumberOfIrrCrops; i++) { // cropFraction[_MDNumberOfIrrCrops] is bare soil Area!
			 numGrowingSeasons = getNumGrowingSeasons (irrIntensity);
			 curCropFraction = MFVarGetFloat(_MDInCropFractionID[i],itemID, 0.0);
			 if (curCropFraction>0){relCropFraction = curCropFraction / sumOfCropFractions;}else{relCropFraction=0;}
			 daysSincePlanted = getDaysSincePlanting(curDay, seasStart,numGrowingSeasons,&_MDirrigCropStruct[i]);
			if (daysSincePlanted > 0) {
			 	addBareSoil=relCropFraction-irrIntensity/ceil(irrIntensity)*relCropFraction;
			    if (relCropFraction>0)cropFraction[i]=relCropFraction -addBareSoil;
				cropFraction[_MDNumberOfIrrCrops]+=addBareSoil;
			}
			else
			{
			cropFraction[i]=0;	
			cropFraction[_MDNumberOfIrrCrops]+=relCropFraction;
			}	
		}
		debugSum=0;
		for (i = 0; i < _MDNumberOfIrrCrops+1; i++) {
			debugSum+=cropFraction[i];
	//	if (itemID==53428)printf("CropFrac %f %i\n",cropFraction[i], itemID);
		}
		
		for (i = 0; i < _MDNumberOfIrrCrops; i++) {
			netIrrDemand=0;cropWR=0;deepPercolation=0;smChange=0;
			relCropFraction   = cropFraction[i];
			numGrowingSeasons = getNumGrowingSeasons (irrIntensity);
			if (relCropFraction > 0) {
			 	daysSincePlanted = getDaysSincePlanting(curDay, seasStart,numGrowingSeasons,&_MDirrigCropStruct[i]);
				if (daysSincePlanted > 0) {
					prevSoilMstDepl = MFVarGetFloat(_MDOutCropDeficitID[i],itemID, 0.0);
					stage = getCropStage(&_MDirrigCropStruct[i], daysSincePlanted);
					cropCoeff =getCropKc(&_MDirrigCropStruct[i], daysSincePlanted, stage);
					cropWR = refETP * cropCoeff;
					float rootDepth = getCurCropRootingDepth (&_MDirrigCropStruct[i],daysSincePlanted);
					rootDepth = 250;
					float riceWaterBalance;
					float nonRiceWaterBalance;
					
					cropDepletionFactor=getCorrDeplFactor(&_MDirrigCropStruct[i], cropWR);
					if (_MDirrigCropStruct[i].cropIsRice==1) {
				//		if (itemID==53428)printf("rice %i \n", itemID);
					if (daysSincePlanted ==1) {
					prevSoilMstDepl=0;
				//	if (itemID==53428)printf("rice day one %i\n", itemID);
					}
				 
						pondingDepth=prevSoilMstDepl+dailyEffPrecip-cropWR-dailyPercolation;
						
						if (pondingDepth>=ReqpondingDepth)
						{
						deepPercolation=pondingDepth -ReqpondingDepth;
						pondingDepth=ReqpondingDepth;
						}
						if (pondingDepth<ReqpondingDepth)
						{
						netIrrDemand=ReqpondingDepth-pondingDepth;
						pondingDepth=ReqpondingDepth;
						
						}
						curDepl=pondingDepth; //so that current ponding depth gets set..		
						smChange=curDepl-prevSoilMstDepl;			 
						deepPercolation+=dailyPercolation;
						riceWaterBalance=dailyEffPrecip+netIrrDemand-cropWR-deepPercolation-smChange;
						if (fabs(riceWaterBalance) > 0.1)printf ("RiceWaterBalance %f prevSMM %f smChange %f \n",riceWaterBalance, prevSoilMstDepl,smChange);
					}
					else {
						
						totAvlWater  = (fldCap  -wltPnt) * rootDepth;
						 
						readAvlWater = totAvlWater * cropDepletionFactor;
					 
				 
						curDepl  = prevSoilMstDepl - dailyEffPrecip + cropWR;
					 
						if (curDepl < 0) {	
							curDepl = 0;
							deepPercolation=dailyEffPrecip - prevSoilMstDepl -cropWR;
						}
						if (curDepl >= totAvlWater) curDepl =totAvlWater;
						if (curDepl >= readAvlWater) {
							netIrrDemand=curDepl;
							curDepl=0;
						}
						smChange=prevSoilMstDepl-curDepl;
						nonRiceWaterBalance=dailyEffPrecip+netIrrDemand-cropWR-deepPercolation-smChange;
						if (fabs(nonRiceWaterBalance) > 0.1)printf ("NONRiceWaterBalance %f prevSMM %f smChange %f \n",riceWaterBalance, prevSoilMstDepl,smChange);
					}
					MFVarSetFloat(_MDOutCropDeficitID[i],itemID,curDepl);
				}
				totalNetIrrDemand+=netIrrDemand*relCropFraction;
				totalCropETP+=cropWR*relCropFraction;
				meanSMChange+=smChange*relCropFraction;
				totalIrrPercolation+=deepPercolation*relCropFraction;
	 		}
			
			//MFVarSetFloat(_MDOutActIrrAreaID, itemID,actuallyIrrArea);
		}
		 // Add Water Balance for bare soil
		    
			relCropFraction   = cropFraction[_MDNumberOfIrrCrops];		
			if (relCropFraction>0) { //Crop is not currently grown. ET from bare soil is equal to ET (initial)
				netIrrDemand=0;
				cropWR=0.2 * refETP;
				prevSoilMstDepl=MFVarGetFloat(_MDOutCropDeficitID[_MDNumberOfIrrCrops],itemID, 0.0);
				totAvlWater  = (fldCap  -wltPnt) * 150;	//assumed RD = 0.15 m
				 
				curDepl  = prevSoilMstDepl - dailyEffPrecip + cropWR;
				if (curDepl < 0) {	
					curDepl = 0;
			 		
					deepPercolation=dailyEffPrecip - prevSoilMstDepl -cropWR;
					}
					 
					if (curDepl >= totAvlWater) {
					    cropWR=totAvlWater-prevSoilMstDepl-dailyEffPrecip;
						curDepl=totAvlWater;
						
				//		if (itemID==58598) printf("her FC %f WP %f curDepl %f prevDepl %f totAvlWater %f cropWR %f dailyEffPrecip %f\n",fldCap, wltPnt,curDepl,prevSoilMstDepl, totAvlWater, cropWR, dailyEffPrecip);								
					}
					 
					 
					smChange=prevSoilMstDepl-curDepl;
					float bareSoilBalance;
					bareSoilBalance=dailyEffPrecip -smChange- cropWR - deepPercolation;
			//	 	if (fabs(bareSoilBalance >0.1)) printf ("bare SMBalance!! precip %f cropWR %f smchange %f dp %f\n ",dailyEffPrecip , cropWR ,smChange, deepPercolation );
  					MFVarSetFloat(_MDOutCropDeficitID[_MDNumberOfIrrCrops],itemID,curDepl );
  				
  					totalCropETP+=cropWR*relCropFraction;
  					meanSMChange+=smChange*relCropFraction;
  					totalIrrPercolation+=deepPercolation*relCropFraction;
  					
				}
			
					
			
			
		
		float totGrossDemand = getIrrGrossWaterDemand(totalNetIrrDemand, irrEffeciency);
		float loss=0;
		loss= (totGrossDemand-totalNetIrrDemand)+(dailyPrecip-dailyEffPrecip);
		float bal = dailyEffPrecip+totalNetIrrDemand-meanSMChange-totalCropETP-totalIrrPercolation;
		loss= (totGrossDemand-totalNetIrrDemand)+(dailyPrecip-dailyEffPrecip);
		float OUT =totalCropETP+totalIrrPercolation+loss+meanSMChange;
		float IN = totGrossDemand+dailyPrecip;
	//	if (itemID==53428)printf("itemID %iGrossDemn %f DailyPrecip %f meanSMChange  %f \n",itemID,totGrossDemand,dailyPrecip,meanSMChange);
	//	if (itemID==53428)printf("ItemID %i CropET %f perc %f loss %f \n",itemID,totalCropETP,totalIrrPercolation,loss);
		//
	//    if (fabs(IN-OUT) > 0.1) CMmsgPrint (CMmsgAppError,"WaterBalance in MDIrrigation!!! IN %f OUT %f BALANCE %f LOSS %f %i DEMAND %f %i EffPrecip %f   itemID %i \n", IN, OUT, IN-OUT, loss, itemID, totGrossDemand, itemID, dailyEffPrecip,itemID);
	 //   if (itemID==48774)printf ("WaterBalance in MDIrrigation!!! IN %f OUT %f BALANCE %f LOSS %f %i DEMAND %f %i EffPrecip %f   itemID %i \n", IN, OUT, IN-OUT, loss, itemID, totGrossDemand, itemID, dailyEffPrecip,itemID);
	 	 

// convert all outputs to cell Area
		totalNetIrrDemand = totalNetIrrDemand* irrAreaFraction;
		totGrossDemand = totGrossDemand* irrAreaFraction;
		totalCropETP=totalCropETP * irrAreaFraction;
		totalIrrPercolation=(totalIrrPercolation+loss)*irrAreaFraction;
		
		if (totGrossDemand < 0 ) {
			CMmsgPrint(CMmsgUsrError, "Net %f GrossDemand %f irrEffeciency %f  Area %f, ceilIrrInten %f \n",totalNetIrrDemand, totalGrossIrrDemand,irrEffeciency,irrAreaFraction, ceil(irrIntensity));
		}
 
		if (isnan(totGrossDemand))
			CMmsgPrint(CMmsgUsrError,"GrossDEmand  nan! LAT %f LON %f irrAreaFraction %f %i\n", MFModelGetLatitude(itemID), MFModelGetLongitude(itemID), irrAreaFraction,itemID);
		if (totalGrossIrrDemand < 0) {	
			CMmsgPrint (CMmsgAppError,"Error in Irrigation, Demand , 0: LON % LAT % Intensity % Efficiency %f FC% WP%\n ",MFModelGetLongitude(itemID), MFModelGetLatitude(itemID), irrIntensity, irrEffeciency, fldCap, wltPnt);
		}
		if (totalIrrPercolation < 0){
			CMmsgPrint (CMmsgUsrError, "Perco %f demand %f IrrArea %f itemID %i\n", totalIrrPercolation, totalGrossIrrDemand, irrAreaFraction,itemID);
		}
		MFVarSetFloat(_MDOutIrrAreaSMChangeID,    itemID, meanSMChange);
		MFVarSetFloat(_MDOutNetIrrDemandID,       itemID, totalNetIrrDemand);
		MFVarSetFloat(_MDOutGrossIrrDemandID,     itemID, totGrossDemand);
		MFVarSetFloat(_MDOutIrrigationDrainageID, itemID, totalIrrPercolation);
		MFVarSetFloat(_MDOutTotalCropETPDepthID,  itemID, totalCropETP);	
	}
	else { // cell is not irrigated at all
		MFVarSetFloat(_MDOutIrrAreaSMChangeID,    itemID, 0.0);
 		MFVarSetFloat(_MDOutNetIrrDemandID,       itemID, 0.0);
		MFVarSetFloat(_MDOutGrossIrrDemandID,     itemID, 0.0);
		MFVarSetFloat(_MDOutIrrigationDrainageID, itemID, 0.0);
		MFVarSetFloat(_MDOutTotalCropETPDepthID,  itemID, 0.0);	
	}
}

 
	 
int MDIrrigationDef() {
	int i;
   if (_MDOutGrossIrrDemandID != MFUnset)	return (_MDOutGrossIrrDemandID);
	float par;
	const char *optStr1;
	int optID=MFUnset;
	const char *optStr, *optName = MDOptIrrReferenceET;
	const char *options [] = {"Hamon","FAO", (char *) NULL };
	MFDefEntering("Irrigation");
	 
	if (((optStr1 = MFOptionGet(MDParIrrDailyPercolationRate)) != (char *) NULL) && (sscanf(optStr1, "%f", &par) == 1))
		_MDParIrrDailyPercolation = par;
		
	 
		if ((optStr = MFOptionGet (optName)) != (char *) NULL) optID = CMoptLookup (options,optStr,true);
	switch(optID)
	{
	case MFUnset: printf ("Unknown reference ETP option!");return CMfailed;break;
	case 0: if ((_MDInRefETPID        = MDHamonReferenceETPDef ()) == CMfailed) return CMfailed;break;
	case 1: if ((_MDInRefETPID        = MDFAOReferenceETPDef   ()) == CMfailed) return CMfailed;break;
	}
	
			
	printf ("OptionIrrRef %i\n", optID);
 		
	if ((_MDInPrecipID        = MDPrecipitationDef ())     == CMfailed) return (CMfailed);
 
	 
	 		 
	if ((_MDInWltPntID        = MFVarGetID (MDVarWiltingPoint,           "mm/m", MFInput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);	
	if ((_MDInFldCapaID       = MFVarGetID (MDVarFieldCapacity,          "mm/m", MFInput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);	
	if ((_MDGrowingSeason1ID  = MFVarGetID (MDVarIrrGrowingSeason1Start, "DoY",  MFInput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);
	if ((_MDGrowingSeason2ID  = MFVarGetID (MDVarIrrGrowingSeason2Start, "DoY",  MFInput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);
					
	if ((_MDInSPackChgID = MFVarGetID (MDVarSnowPackChange, "mm",   MFInput, MFFlux,  MFBoundary)) == CMfailed) return CMfailed;
	if ((_MDGrowingSeason3ID  = MFVarGetID (MDVarIrrGrowingSeason3Start, "DoY",  MFInput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);
	if ((_MDGrowingSeasonStartCalcID  = MFVarGetID (MDVarStartGrowingSeasonCalc, "DoY",  MFOutput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);
	if ((_MDGrowingSeasonEndCalcID  = MFVarGetID (MDVarEndGrowingSeasonCalc, "DoY",  MFOutput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);
	if ((_MDInAirTemperatureID  = MFVarGetID (MDVarAirTemperature, "degC",  MFInput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);
	if ((_MDInIrrIntensityID  = MFVarGetID (MDVarIrrIntensity,           "-",    MFInput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);
	if ((_MDInIrrEfficiencyID = MFVarGetID (MDVarIrrEfficiency,          "-",    MFInput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);
	if ((_MDInIrrAreaID       = MFVarGetID (MDVarIrrAreaFraction,        "%",    MFInput,  MFState, MFBoundary)) == CMfailed) return (CMfailed);
	if ((optStr = MFOptionGet (MDParIrrigationCropFileName)) != (char *) NULL) CropParameterFileName = optStr;
  	if (readCropParameters (CropParameterFileName) == CMfailed) {
		CMmsgPrint(CMmsgUsrError,"Error reading crop parameter file   : %s \n", CropParameterFileName);
		return CMfailed;
	}
			//Ouputs
 			if ((_MDOutGrossIrrDemandID     = MFVarGetID (MDVarIrrGrossDemand,          "mm", MFOutput, MFFlux, MFBoundary)) == CMfailed) return (CMfailed);
			if ((_MDOutNetIrrDemandID       = MFVarGetID (MDVarIrrNetIrrigationWaterDemand,            "mm", MFOutput, MFFlux, MFBoundary)) == CMfailed) return (CMfailed);
			if ((_MDOutIrrAreaSMChangeID    = MFVarGetID (MDVarIrrSoilMoistureChange,                  "mm", MFOutput, MFFlux, MFBoundary)) == CMfailed) return (CMfailed);
			if ((_MDOutIrrigationDrainageID = MFVarGetID (MDVarIrrReturnFlow,                    "mm", MFOutput, MFFlux, MFBoundary)) == CMfailed) return (CMfailed);
			if ((_MDOutTotalCropETPDepthID  = MFVarGetID (MDVarIrrCropETP,                             "mm", MFOutput, MFFlux, MFBoundary)) == CMfailed) return (CMfailed);
		//	if ((_MDOutActIrrAreaID	        = MFVarGetID (MDVarActuallyIrrArea,			               "-",  MFOutput, MFState, MFBoundary)) == CMfailed) return (CMfailed);
 			MDParNumberOfCrops = _MDNumberOfIrrCrops;
			CMmsgPrint(CMmsgInfo,"Number of crops read: %i \n", _MDNumberOfIrrCrops);
			for (i=0;i<_MDNumberOfIrrCrops;i++) {
 			 	char frac []= { "CropFraction_    " };
				char def [] = { "CropSMDef_    " };
				sprintf (frac + 13,"%02d", i + 1);
				sprintf (def  + 10,"%02d", i + 1);
		
				//Input Fraction of crop type per cell
				if (CMfailed == (_MDInCropFractionID[i] = MFVarGetID (frac,"mm", MFInput, MFState, MFBoundary))) {
					CMmsgPrint (CMmsgUsrError, "CMfailed in MDInCropFractionID \n");
					return CMfailed;
				}
				//Output Soil Moisture Deficit per croptype
//				if ((_MDOutCropDeficitID[i]=MFVarGetID (def,"mm", MFOutput,MFState, MFInitial))==CMfailed) {
//					CMmsgPrint (CMmsgUsrError,"MFFAult in MDCropDeficit\n");
//					return CMfailed;
//				}
			}
			
			for (i=0;i<_MDNumberOfIrrCrops+1;i++) {
 			 	char frac []= { "CropFraction_    " };
				char def [] = { "CropSMDef_    " };
				sprintf (frac + 13,"%02d", i + 1);
				sprintf (def  + 10,"%02d", i + 1);
		
				//Input Fraction of crop type per cell
		/*		if (CMfailed == (_MDInCropFractionID[i] = MFVarGetID (frac,"mm", MFInput, MFState, MFBoundary))) {
					CMmsgPrint (CMmsgUsrError, "CMfailed in MDInCropFractionID \n");
					return CMfailed;
				}*/
				//Output Soil Moisture Deficit per croptype
				if ((_MDOutCropDeficitID[i]=MFVarGetID (def,"mm", MFOutput,MFState, MFInitial))==CMfailed) {
					CMmsgPrint (CMmsgUsrError,"MFFAult in MDCropDeficit\n");
					return CMfailed;
				}
			}
			
			//_MDOutGrossIrrDemandID = MFVarSetFunction (_MDOutGrossIrrDemandID,_MDIrrigationWater);
//			break;
//		default: MFOptionMessage (optName, optStr, options); return (CMfailed);
 if(MFModelAddFunction (_MDIrrigationWater) == CMfailed) return (CMfailed);
			
	MFDefLeaving("Irrigation");
	return (_MDOutGrossIrrDemandID);
}

static int getNumGrowingSeasons(float irrIntensity){
	
	return ceil(irrIntensity);
	
}
static float getIrrGrossWaterDemand(float netIrrDemand, float IrrEfficiency) {
	if (IrrEfficiency <= 0) { // no data, set to average value		
		IrrEfficiency=38;
	} 
	return netIrrDemand / (IrrEfficiency/100);
}

static float getCorrDeplFactor(const MDIrrigatedCrop * pIrrCrop, float dailyETP) {
	float cropdeplFactor = pIrrCrop->cropDepletionFactor + 0.04 * (5 - dailyETP);
    if (cropdeplFactor <= 0.1) cropdeplFactor = 0.1;
	if (cropdeplFactor >= 0.8) cropdeplFactor = 0.8;
	return cropdeplFactor;
}

static float getEffectivePrecipitation(float dailyPrecip) {
	float effPrecip;
	
	if (dailyPrecip == 0) effPrecip = 0;
	if (dailyPrecip < 8.3)
		return dailyPrecip * (4.17 - 0.2 * dailyPrecip) / 4.17;
	else
		effPrecip = dailyPrecip * .1 + 4.17;
	if (dailyPrecip < 0.2) effPrecip = 0;
	return effPrecip;
}

static float getCurCropRootingDepth(MDIrrigatedCrop * pIrrCrop, int dayssinceplanted) {
	float rootDepth;
	float totalSeasonLenth;
	totalSeasonLenth =pIrrCrop->cropSeasLength[0] +	pIrrCrop->cropSeasLength[1] +	pIrrCrop->cropSeasLength[2] + pIrrCrop->cropSeasLength[3];
    rootDepth= pIrrCrop->cropRootingDepth *( 0.5 + 0.5 * sin(3.03 *   (dayssinceplanted  /  totalSeasonLenth) - 1.47));
	if (rootDepth > 2)		CMmsgPrint (CMmsgDebug, "RootDepth correct ?? %f \n",rootDepth);
 	if (rootDepth <0.15) rootDepth =.15;
	return rootDepth;
}

static int getCropStage(const MDIrrigatedCrop * pIrrCrop, int daysSincePlanted) {
	int stage = 0;
	float totalSeasonLenth;
	totalSeasonLenth =
	pIrrCrop->cropSeasLength[0] +
	pIrrCrop->cropSeasLength[1] +
	pIrrCrop->cropSeasLength[2] + pIrrCrop->cropSeasLength[3];
    if (daysSincePlanted <= totalSeasonLenth)
	stage = 4;

    if (daysSincePlanted <=
	pIrrCrop->cropSeasLength[0] +
	pIrrCrop->cropSeasLength[1] + pIrrCrop->cropSeasLength[2])
	stage = 3;

    if ((daysSincePlanted <= pIrrCrop->cropSeasLength[0] + pIrrCrop->cropSeasLength[1]))
	stage = 2;

    if (daysSincePlanted <= pIrrCrop->cropSeasLength[0])
	stage = 1;

if (stage ==0 && daysSincePlanted > 0) printf ("Stagexx =0? st0 %f st1 %f st1 %f st2 %f DSP %i TOT %f  \n",  pIrrCrop->cropSeasLength[0], pIrrCrop->cropSeasLength[1],pIrrCrop->cropSeasLength[2], pIrrCrop->cropSeasLength[3] ,daysSincePlanted, totalSeasonLenth);
     return stage;
		if (stage >4)	CMmsgPrint (CMmsgDebug, "stage corrdct ?? %i \n",stage);
}

static float getCropKc(const MDIrrigatedCrop * pIrrCrop, int daysSincePlanted, int curCropStage)
{
	float kc;
if(_MDIrrConstantKc==1)return 1.0;
   //Returns kc depending on the current stage of the growing season
	if (curCropStage == 0) kc = 0.0;		//crop is not currently grown
	if (curCropStage == 1) kc = pIrrCrop->cropKc[0];
	if (curCropStage == 2) {
		int daysInStage = (daysSincePlanted - pIrrCrop->cropSeasLength[0]);
		kc = pIrrCrop->cropKc[0] + (daysInStage /  pIrrCrop->cropSeasLength[1])*(pIrrCrop->cropKc[1]-pIrrCrop->cropKc[0]);
	} 
	if (curCropStage == 3) kc = pIrrCrop->cropKc[1];
	if (curCropStage == 4) {
		int daysInStage4 = (daysSincePlanted - (pIrrCrop->cropSeasLength[0] +  pIrrCrop->cropSeasLength[1] + pIrrCrop->cropSeasLength[2]));
		//kc = pIrrCrop->cropKc[2] -	    (daysInStage4 / pIrrCrop->cropSeasLength[3]) * abs(pIrrCrop->cropKc[3] - pIrrCrop->cropSeasLength[2]);
		kc=pIrrCrop->cropKc[1]+ daysInStage4/  pIrrCrop->cropSeasLength[3] *(pIrrCrop->cropKc[2]-pIrrCrop->cropKc[1]);
		//printf ("Len3 %f kc3 %f daysin4 %i KC %f \n" , pIrrCrop->cropSeasLength[3], pIrrCrop->cropKc[3],daysInStage4,kc);
	}
	if (kc >1.5 )	CMmsgPrint (CMmsgDebug, "kc korrect ?? kc stage dayssinceplanted  kc0 kc1 season0length %f %i %i %f %f %f \n",kc, curCropStage, daysSincePlanted, pIrrCrop->cropKc[0],pIrrCrop->cropKc[1], pIrrCrop->cropSeasLength[0]);
 	return kc;
}

static int getDaysSincePlanting(int DayOfYearModel, int DayOfYearPlanting[numSeasons],int NumGrowingSeasons,const MDIrrigatedCrop * pIrrCrop) {
	int ret=-888;
	float totalSeasonLenth;

	totalSeasonLenth =
	pIrrCrop->cropSeasLength[0] +
	pIrrCrop->cropSeasLength[1] +
	pIrrCrop->cropSeasLength[2] + pIrrCrop->cropSeasLength[3];
	int dayssinceplanted ;	//Default> crop is not grown!
	int i;
	for (i = 0; i < NumGrowingSeasons; i++) {
		dayssinceplanted = DayOfYearModel - DayOfYearPlanting[i];
		if (dayssinceplanted < 0)  dayssinceplanted = 365 + (DayOfYearModel-DayOfYearPlanting[i]);
	   
		if (dayssinceplanted  < totalSeasonLenth) ret = dayssinceplanted;
	}
	if (ret >totalSeasonLenth)	CMmsgPrint (CMmsgDebug, "dayssinceplantedkorrect ?? %i %i \n",ret, DayOfYearModel);
	return ret;
}


static int readCropParameters(const char *filename) {
	FILE *inputCropFile;
	int i = 0;
	if ((inputCropFile = fopen(filename, "r")) == (FILE *) NULL) {
		CMmsgPrint (CMmsgUsrError,"Crop Parameter file could not be opned, filename: %s\n", filename);
	
		return CMfailed;
	}
	else {
		char buffer[512];
		//read headings..
	
		fgets (buffer,sizeof (buffer),inputCropFile);
		while (feof(inputCropFile) == 0) {
			_MDirrigCropStruct =  (MDIrrigatedCrop *) realloc(_MDirrigCropStruct,(i +	 1) *	sizeof(MDIrrigatedCrop));
			_MDInCropFractionID  =(int *) realloc(_MDInCropFractionID,  (i + 1) * sizeof(int));
			_MDOutCropDeficitID 	=(int *) realloc(_MDOutCropDeficitID,(i+1) *sizeof(int));	
			fscanf(inputCropFile, "%i" "%i" "%s" "%s" "%f" "%f" "%f" "%f" "%f" "%f" "%f" "%f" "%f" ,
		       &_MDirrigCropStruct[i].ID,
		       &_MDirrigCropStruct[i].DW_ID,
		       _MDirrigCropStruct[i].cropName,
		       _MDirrigCropStruct[i].cropDistrFileName,
		       &_MDirrigCropStruct[i].cropKc[0],
		       &_MDirrigCropStruct[i].cropKc[1],
		       &_MDirrigCropStruct[i].cropKc[2],
		       &_MDirrigCropStruct[i].cropSeasLength[0],
		       &_MDirrigCropStruct[i].cropSeasLength[1],
		       &_MDirrigCropStruct[i].cropSeasLength[2],
		       &_MDirrigCropStruct[i].cropSeasLength[3],
		       &_MDirrigCropStruct[i].cropRootingDepth,
		       &_MDirrigCropStruct[i].cropDepletionFactor);
				
			if (strcmp(_MDirrigCropStruct[i].cropName , "Rice")==0) _MDirrigCropStruct[i].cropIsRice = 1;
			if (strcmp(_MDirrigCropStruct[i].cropName , "Rice")!=0) _MDirrigCropStruct[i].cropIsRice = 0;
				
			printCrops(&_MDirrigCropStruct[i]);
			i += 1;

			 
		}
	}
	//CMmsgPrint(CMmsgDebug,"Number of crops read: %i \n", i - 1);
	_MDNumberOfIrrCrops = i - 1;	
	return CMsucceeded;
}

static void printCrops(const MDIrrigatedCrop * curCrop) {

	CMmsgPrint(CMmsgDebug,"==++++++++++================  %s ===========================", curCrop->cropName);
	CMmsgPrint(CMmsgDebug,"CurrentCropID %i \n", curCrop->ID);
	CMmsgPrint(CMmsgDebug,"CropName %s	\n ", curCrop->cropName);
	CMmsgPrint(CMmsgDebug,"kc  %f %f %f \n", curCrop->cropKc[0], curCrop->cropKc[1], curCrop->cropKc[2]);
	CMmsgPrint(CMmsgDebug,"Length./Total . %f %f %f %f %i \n",
			curCrop->cropSeasLength[0],
			curCrop->cropSeasLength[1],
			curCrop->cropSeasLength[2],
			curCrop->cropSeasLength[3],
			getTotalSeasonLength(curCrop));
	CMmsgPrint(CMmsgDebug,"RootingDepth %f \n", curCrop->cropRootingDepth);
	CMmsgPrint(CMmsgDebug,"DepleationFactors %f \n", curCrop->cropDepletionFactor);
	CMmsgPrint(CMmsgDebug,"IsRice %i \n", curCrop->cropIsRice);
	fflush(stdout);
}

static int getTotalSeasonLength(const MDIrrigatedCrop * pIrrCrop) {
	return pIrrCrop->cropSeasLength[0] + pIrrCrop->cropSeasLength[1] +
	pIrrCrop->cropSeasLength[2] + pIrrCrop->cropSeasLength[3];
}
