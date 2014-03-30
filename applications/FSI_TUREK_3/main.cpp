#include "ElemType.hpp"
#include "MultiLevelProblem.hpp"
#include "TransientSystem.hpp"
#include "NumericVector.hpp"
#include "PetscVector.hpp"
#include "LinearEquationSolver.hpp"
#include "Solid.hpp"
#include "Fluid.hpp"
#include "Parameter.hpp"
#include <iostream>
#include "FemTTUInit.hpp"
#include "../include/FSIassembly.hpp"


using std::cout;
using std::endl;

double SetVariableTimeStep(const double time);

bool SetBoundaryCondition(const double &x, const double &y, const double &z,const char name[], 
		double &value, const int FaceName, const double = 0.);

bool SetRefinementFlag(const double &x, const double &y, const double &z, const int &ElemGroupNumber,const int &level);

//------------------------------------------------------------------------------------------------------------------

int main(int argc,char **args) {
  
  /// Init Petsc-MPI communicator
  FemTTUInit mpinit(argc,args,MPI_COMM_WORLD);

  unsigned short nm,nr;
  std::cout<<"#MULTIGRID levels? (>=1) \n";
  //std::cin>>nm;
  nm=4;

  std::cout<<"#MAX_REFINEMENT levels? (>=0) \n";
  //std::cin>>nr;
  nr=0;
  int tmp=nm;
  nm+=nr;
  nr=tmp;

  char *infile = new char [50];
  
  sprintf(infile,"./input/fsifirst.neu");

  double Lref = 1.;
  double Uref = 1.;
  double rhof = 1000.;
  double muf = 1.;
  double rhos = 1000;
  double ni = 0.4;
  double E = 5600000;
  
  MultiLevelProblem ml_prob(nm,nr,infile,"fifth",Lref,SetRefinementFlag);

  Parameter par(Lref,Uref);
  
  // Generate Solid Object
  Solid solid(par,E,ni,rhos,"Neo-Hookean");
  cout << "Solid properties: " << endl;
  cout << solid << endl;
  
  // Generate Fluid Object
  Fluid fluid(par,muf,rhof,"Newtonian");
  cout << "Fluid properties: " << endl;
  cout << fluid << endl;

  // Add fluid object
  ml_prob.parameters.set<Fluid>("Fluid") = fluid;
  
  // Add Solid Object
  ml_prob.parameters.set<Solid>("Solid") = solid;

  //Start System Variables
  ml_prob.AddSolution("DX","biquadratic",2);
  ml_prob.AddSolution("DY","biquadratic",2);
  ml_prob.AssociatePropertyToSolution("DX","Displacement"); // Add this line
  ml_prob.AssociatePropertyToSolution("DY","Displacement"); // Add this line 
  ml_prob.AddSolution("U","biquadratic",2);
  ml_prob.AddSolution("V","biquadratic",2);
  ml_prob.AddSolution("AX","biquadratic",1,0);
  ml_prob.AddSolution("AY","biquadratic",1,0);
  // Since the Pressure is a Lagrange multiplier it is used as an implicit variable
  ml_prob.AddSolution("P","disc_linear",1);
  ml_prob.AssociatePropertyToSolution("P","Pressure"); // Add this line

  //Initialize (update Init(...) function)
  ml_prob.Initialize("All");

  //Set Boundary (update Dirichlet(...) function)
  ml_prob.AttachSetBoundaryConditionFunction(SetBoundaryCondition);
  ml_prob.GenerateBdc("DX","Steady");
  ml_prob.GenerateBdc("DY","Steady");
  ml_prob.GenerateBdc("U","Time_dependent");
  ml_prob.GenerateBdc("V","Steady");
  ml_prob.GenerateBdc("AX","Steady");
  ml_prob.GenerateBdc("AY","Steady");
  ml_prob.GenerateBdc("P","Steady");

  
    //create systems
  // add the system FSI to the MultiLevel problem
  TransientNonlinearImplicitSystem & system = ml_prob.add_system<TransientNonlinearImplicitSystem> ("Fluid-Structure-Interaction");
  system.AddSolutionToSytemPDE("DX");
  system.AddSolutionToSytemPDE("DY");
  system.AddSolutionToSytemPDE("U");
  system.AddSolutionToSytemPDE("V");
  system.AddSolutionToSytemPDE("P");
  
  // init all the systems
  ml_prob.init();
  
  std::vector<std::string> mov_vars;
  mov_vars.push_back("DX");
  mov_vars.push_back("DY");
  ml_prob.SetMovingMesh(mov_vars);
  ml_prob.MarkStructureNode();
 
  // System Fluid-Structure-Interaction
  system.AttachAssembleFunction(AssembleMatrixResFSI);  
  system.SetMaxNumberOfLinearIterations(1);
  system.SetAbsoluteConvergenceTolerance(1.e-8);  
  system.SetMgType(V_CYCLE);
  system.SetMaxNumberOfNonLinearIterations(15);
  system.SetDirichletBCsHandling(ELIMINATION);

  // time loop parameter
  system.AttachGetTimeIntervalFunction(SetVariableTimeStep);
  const unsigned int n_timesteps = 5;
  const unsigned int write_interval = 1;
  
  for (unsigned time_step = 0; time_step < n_timesteps; time_step++) {
   
    // Solving Fluid-Structure-Interaction system
    std::cout << std::endl;
    std::cout << " *********** Fluid-Structure-Interaction ************  " << std::endl;
    ml_prob.get_system<TransientNonlinearImplicitSystem>("Fluid-Structure-Interaction").solve();
   
    //The update of the acceleration must be done before the update of the other variables
    ml_prob.get_system<TransientNonlinearImplicitSystem>("Fluid-Structure-Interaction").NewmarkAccUpdate();
    
    //update Solution
    ml_prob.get_system<TransientNonlinearImplicitSystem>("Fluid-Structure-Interaction").UpdateSolution();

    // print solution
    if ( !(time_step%write_interval) ) {
        
      //print solution 
      std::vector<std::string> print_vars;
      print_vars.push_back("DX");
      print_vars.push_back("DY");
      print_vars.push_back("U");
      print_vars.push_back("V");
      print_vars.push_back("P");
      
      ml_prob.printsol_vtu_inline("biquadratic",print_vars,time_step);
    }
  
  } //end loop timestep
  

  // Destroy all the new systems
  ml_prob.clear();
   
  /// Destroy the last PETSC objects
  ml_prob.FreeMultigrid(); 
  
  
  
  
  
  
//   //Set Time step information
//   nl_td_ml_prob.SetTimeStep(0.05);
//   nl_td_ml_prob.SetPrintTimeStep(1);
//   nl_td_ml_prob.SetSaveTimeStep(3330);
//   nl_td_ml_prob.SetNumTimeSteps(1000);  //165   
// // nl_td_ml_prob.InitializeFromRestart(5);
//   nl_td_ml_prob.AttachSetTimeStepFunction(SetVariableTimeStep);
//   
// 
//   std::vector<std::string> mov_vars;
//   mov_vars.push_back("DX");
//   mov_vars.push_back("DY");
//   nl_td_ml_prob.SetMovingMesh(mov_vars);
//   nl_td_ml_prob.MarkStructureNode();
//  
//   //Solver Configuration 
//  
//   
//   
//   nl_td_ml_prob.AddPde("FSI");
//   nl_td_ml_prob.AddSolutionToSolPdeIndex("FSI","DX"); 
//   nl_td_ml_prob.AddSolutionToSolPdeIndex("FSI","DY");
//   //nl_td_ml_prob.AddSolutionToSolPdeIndex("FSI","DZ");
//   nl_td_ml_prob.AddSolutionToSolPdeIndex("FSI","U");
//   nl_td_ml_prob.AddSolutionToSolPdeIndex("FSI","V");
//   //nl_td_ml_prob.AddSolutionToSolPdeIndex("FSI","W");
//   nl_td_ml_prob.AddSolutionToSolPdeIndex("FSI","P");
//   
//   // create Multigrid (PRLO, REST, MAT, VECs) based on MGIndex
//   nl_td_ml_prob.CreatePdeStructure();
//   
//   nl_td_ml_prob.SetDirichletBCsHandling("FSI","Elimination");
//   
//   //Solver I (Gmres)
// //   nl_td_ml_prob.SetSmoother("Gmres");
// //   nl_td_ml_prob.SetTolerances("FSI",1.e-12,1.e-20,1.e+50,6);
//   
//   
// //   //Solver II (Vanka-smoother-MPSC)
//   nl_td_ml_prob.AddStabilization("FSI",true);
//   nl_td_ml_prob.SetSolverFineGrids("FSI","GMRES");
//   nl_td_ml_prob.SetPreconditionerFineGrids("FSI","LU");
//   nl_td_ml_prob.SetVankaSchurOptions(false,0);
//   nl_td_ml_prob.SetTolerances("FSI",1.e-12,1.e-20,1.e+50,1);
//   nl_td_ml_prob.SetSchurTolerances("FSI",1.e-12,1.e-20,1.e+50,4);
//   nl_td_ml_prob.SetDimVankaBlock("FSI",4);                //2^lev 1D 4^lev 2D 8^lev 3D
// 
//   //End System Variables; ==============================
// 
//   // START EQUATIONS =================================
// 
//   // Start FSI Muligrid Block
//   nl_td_ml_prob.AttachAssembleFunction(AssembleMatrixResFSI);
// 
// 
//   
//   // create index of solutions to be to used in the Vanka Smoother
//   nl_td_ml_prob.ClearVankaIndex();
//   nl_td_ml_prob.AddToVankaIndex("FSI","DX");
//   nl_td_ml_prob.AddToVankaIndex("FSI","DY");
//   nl_td_ml_prob.AddToVankaIndex("FSI","U");
//   nl_td_ml_prob.AddToVankaIndex("FSI","V");
//   nl_td_ml_prob.AddToVankaIndex("FSI","P");
// 
//   
//   for (unsigned time_step = nl_td_ml_prob.GetInitTimeStep(); time_step < nl_td_ml_prob.GetInitTimeStep() + nl_td_ml_prob.GetNumTimeSteps(); 
//        time_step++) {
//    
//     //Solve with V-cycle or F-cycle
//     nl_td_ml_prob.Solve("FSI",30,1,1,"V-Cycle");
//   
//     //The update of the acceleration must be done before the update of the other variables
//     //update time step
//     nl_td_ml_prob._NewmarkAccUpdate();
// 
//     //update Solution
//     nl_td_ml_prob._UpdateSolution();
// 
//     //print solution for restart
//     if ( !(time_step%nl_td_ml_prob.GetSaveTimeStep()) ) {
//       nl_td_ml_prob.SaveData();
//     }
// 
//     // print solution
//     if ( time_step>9000 || time_step==1 || !(time_step%nl_td_ml_prob.GetPrintTimeStep()) ) {
//       std::vector<std::string> print_vars;
//       print_vars.resize(5);
//       print_vars[0] = "DX";
//       print_vars[1] = "DY";
//       print_vars[2] = "U";
//       print_vars[3] = "V";
//       print_vars[4] = "P";
// 
//       //nl_td_ml_prob.printsol_vtu_inline("biquadratic",print_vars);
//       nl_td_ml_prob.printsol_vtu_inline("linear",print_vars);
//       //nl_td_ml_prob.printsol_xdmf_hdf5("biquadratic",print_vars);
//        
//     }
//   
//   } //end loop timestep
//   
//   //print the XDMF time archive
//   //nl_td_ml_prob.printsol_xdmf_archive("biquadratic");
// 
//   // Delete Multigrid (PRLO, REST, MAT, VECs) based on MGIndex
//   nl_td_ml_prob.DeletePdeStructure();
// 
//   // End FSI Muligrid Block
//   // Destroy the last PETSC objects
//   nl_td_ml_prob.FreeMultigrid();
// 
//   delete [] infile;
  return 0;
}


bool SetRefinementFlag(const double &x, const double &y, const double &z, const int &elemgroupnumber,const int &level) {
  bool refine=0;

  //refinemenet based on elemen group number
  if (elemgroupnumber==5) refine=1;
  if (elemgroupnumber==6) refine=1;
  if (elemgroupnumber==7 && level<5) refine=1;

  return refine;

}

//---------------------------------------------------------------------------------------------------------------------

double SetVariableTimeStep(const double time) {
 if(time < 4.) {
   return 0.01;
 } 
 else {
   return 0.001; 
 }
}

//---------------------------------------------------------------------------------------------------------------------

bool SetBoundaryCondition(const double &x, const double &y, const double &z,const char name[], double &value, const int facename, const double time) {
  bool test=1; //dirichlet
  value=0.;
  if(!strcmp(name,"U")) {
    if(1==facename){   //inflow
      test=1;
      double um = 2.0;
      if(time < 2.0) {
   	value=1.5*um*4.0/0.1681*y*(0.41-y)*0.5*(1. - cos(0.5*3.141592653589793*time));
       }
      else {
        value=1.5*um*4.0/0.1681*y*(0.41-y);
       }
    }  
    else if(2==facename ){  //outflow
     test=0;
 //    test=1;
     value=0.;
    }
    else if(3==facename ){  // no-slip fluid wall
      test=1;
      value=0.;	
    }
    else if(4==facename ){  // no-slip solid wall
      test=1;
      value=0.;
    }
  }  
  else if(!strcmp(name,"V")){
    if(1==facename){            //inflow
      test=1;
      value=0.;
    }  
    else if(2==facename ){      //outflow
     test=0;
 //    test=1;
     value=0.;
    }
    else if(3==facename ){      // no-slip fluid wall
      test=1;
      value=0;
    }
    else if(4==facename ){      // no-slip solid wall
      test=1;
      value=0.;
    }
  }
  else if(!strcmp(name,"W")){
    if(1==facename){
      test=1;
      value=0.;
    }  
    else if(2==facename ){  
      test=1;
      value=0.;
    }
    else if(3==facename ){  
      test=1;
      value=0.;
    }
    else if(4==facename ){  
      test=1;
      value=0.;
    }
  }
  else if(!strcmp(name,"P")){
    if(1==facename){
      test=0;
      value=0.;
    }  
    else if(2==facename ){  
      test=0;
      value=0.;
    }
    else if(3==facename ){  
      test=0;
      value=0.;
    }
    else if(4==facename ){  
      test=0;
      value=0.;
    }
  }
  else if(!strcmp(name,"DX")){
    if(1==facename){         //inflow
      test=1;
      value=0.;
    }  
    else if(2==facename ){   //outflow
     test=1;
     value=0.;
    }
    else if(3==facename ){   // no-slip fluid wall
      test=0; //0
      value=0.;	
    }
    else if(4==facename ){   // no-slip solid wall
      test=1;
      value=0.;
    }
  }
  else if(!strcmp(name,"DY")){
    if(1==facename){         //inflow
      test=0; // 0
      value=0.;
    }  
    else if(2==facename ){   //outflow
     test=0; // 0
     value=0.;
    }
    else if(3==facename ){   // no-slip fluid wall
      test=1;
      value=0.;	
    }
    else if(4==facename ){   // no-slip solid wall
      test=1;
      value=0.;
    }
  }
  else if(!strcmp(name,"DZ")){
    if(1==facename){         //inflow
      test=1;
      value=0.;
    }  
    else if(2==facename ){   //outflow
     test=1;
     value=0.;
    }
    else if(3==facename ){   // no-slip fluid wall
      test=1;
      value=0.;	
    }
    else if(4==facename ){   // no-slip solid wall
      test=1;
      value=0.;
    }
  }
  else if (!strcmp(name,"AX")) {
    test=0;
    value=0;
  }
  else if (!strcmp(name,"AY")) {
    test=0;
    value=0;
  }
  else if (!strcmp(name,"AZ")) {
    test=0;
    value=0;
  }
  return test;
}
