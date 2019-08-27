//#include <Rcpp.h>
//using namespace Rcpp;
#include "CStringManipulation.h"


  int    CStringManipulation::length		     ( std::string   tStr )
  {
  	int lengthStr = 0 ;
  	lengthStr = tStr.length() ;
  	return (lengthStr) ;
  }
  
  
  // Returns the left hand side of a std::string of format :  "left hand side = right hand side"  returns "left hand side"
  std::string CStringManipulation::LeftSideOfEqual(std::string tStr)
  {
     // lowerCase(trim(tStr.copy(tStr,1,tStr.find("=")-1))) ;
  
  	size_t i_equals = tStr.find("=") ;
  	// tStr.copy((char *) tStr.c_str(),1,i_equals) ;
  	if (i_equals > 0)
  	{
  		tStr = tStr.substr(0,i_equals) ;
  		tStr = trim(tStr) ;
  		tStr = lowerCase(tStr) ;
  		return tStr ;
  	}
  	else
  	{
  		tStr = "" ;
  		return tStr ;	
  	}
  } 
  
  // Returns the left hand side of a std::string of format :  "left hand side = right hand side"  returns "left hand side"
  std::string CStringManipulation::LeftSideOfEqual(std::string tStr, bool forceLowerCase)
  {
     // lowerCase(trim(tStr.copy(tStr,1,tStr.find("=")-1))) ;
  
  	size_t i_equals = tStr.find("=") ;
  	// tStr.copy((char *) tStr.c_str(),1,i_equals) ;
  	if (i_equals > 0)
  	{
  		tStr = tStr.substr(0,i_equals) ;
  		tStr = trim(tStr) ;
  		if (forceLowerCase)
  			tStr = lowerCase(tStr) ;
  		return tStr ;
  	}
  	else
  	{
  		tStr = "" ;
  		return tStr ;	
  	}
  } 
  
  
  
  // Returns the left hand side of a std::string of format :  "left hand side inChar right hand side"  returns "left hand side "
  std::string CStringManipulation::LeftSideOfString(std::string inChar, std::string tStr, bool forceLowerCase) // returns the left hand side std::string before first occurence of inChar
  {
     // lowerCase(trim(tStr.copy(tStr,1,tStr.find("=")-1))) ;
  
  	size_t i_equals = tStr.find(inChar) ;
  	// tStr.copy((char *) tStr.c_str(),1,i_equals) ;
  	if (i_equals > 0)
  	{
  		tStr = tStr.substr(0,i_equals) ;
  		tStr = trim(tStr) ;
  		if (forceLowerCase)
  			tStr = lowerCase(tStr) ;
  		return tStr ;
  	}
  	else
  	{
  		tStr = "" ;
  		return tStr ;	
  	}
  } 
  
  
  
  // returns the right hand side of a std::string (in lower case) of format :  "left hand side = right hand side"  returns "right hand side"
  std::string CStringManipulation::RightSideOfString(std::string inChar, std::string tStr, bool forceLowerCase)  // returns the riight hand side std::string after first occurence of inChar
  {
  
    int i_equals = tStr.find(inChar) ;  // == pos('=',tStr)
      
  	if (i_equals >= 0)
  	{
  		tStr = tStr.substr(i_equals+1,tStr.length() -1) ;
  		tStr = trim(tStr) ;
  		//lowerCase(tStr) ;
  		if (forceLowerCase)
  			tStr = lowerCase(tStr) ;
  		return tStr ;
  	}
  	else
  	{
  		tStr = "" ;
  		return tStr ;	
  	}
  } 
  
  
  
  // returns the right hand side of a std::string (in lower case) of format :  "left hand side = right hand side"  returns "right hand side"
  std::string CStringManipulation::RightSideOfEqual(std::string tStr, bool forceLowerCase)
  {
  //  return = lowerCase(trim(copy(tStr,pos('=',tStr)+1,length(tStr)-   length(    copy(tStr,1,     pos('=',tStr)))))) ;
    int i_equals = tStr.find("=") ;  // == pos('=',tStr)
      
  	if (i_equals >= 0)
  	{
  		tStr = tStr.substr(i_equals+1,tStr.length() -1) ;
  		tStr = trim(tStr) ;
  		//lowerCase(tStr) ;
  		if (forceLowerCase)
  			tStr = lowerCase(tStr) ;
  		return tStr ;
  	}
  	else
  	{
  		tStr = "" ;
  		return tStr ;	
  	}
  } 
  
  
  // converts upper case characters in tStr to lower case
  std::string CStringManipulation::lowerCase(std::string tStr)
  {
    /* char tchar ;
     int i = 0  ;
     size_t tlength = tStr.length() ;
  
     for (i = 0 ; i < tlength ; i++)
     {
  		tchar = tStr[i] ;
  		if ((tchar < 91) &&  (tchar > 64))
  			tchar = tchar + 32 ;
  		tStr[i] = tchar ;
     }*/
     std::transform(tStr.begin(), tStr.end(), tStr.begin(), ::tolower);
  
     return tStr ;
  }
  
  
  std::string  CStringManipulation::CStringManipulation::trim( std::string tStr )
  {
  	 long long numChars = 0 ;
  	//size_t numChars = 0 ;
  
  	if (tStr.length() > 0)
  	{
  		while ((numChars < tStr.length()) && (tStr[numChars] <= 32)  )  
  		{numChars++ ;}
  
  		size_t length = tStr.length() ;
  		tStr = tStr.substr( numChars, length - numChars  ) ; 
  
  		if (tStr.length() > 0)
  		{
  			numChars = tStr.length() - 1 ;
  			while ((numChars >= 0) && (tStr[numChars] <=32) )  
  			{numChars-- ;}
  
  			if (numChars > 0)
  			tStr = tStr.substr( 0, numChars + 1) ; 
  		}
  	}
  
  	return tStr ;
  }

// end of class Cstd::stringMANIPULATION