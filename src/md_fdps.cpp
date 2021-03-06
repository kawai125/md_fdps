/**************************************************************************************************/
/**
* @file  md_main.cpp
* @brief main routine of "md_fdps" code.
*          This code is using the Framework for Developing Particle Simulator (FDPS).
*          https://github.com/FDPS
*/
/**************************************************************************************************/

//--- standard library header
#include <chrono>

//--- FDPS header
#include <particle_simulator.hpp>
#include <particle_mesh.hpp>

//--- external library for MD
#include <molecular_dynamics_ext.hpp>

//--- MD function headers
//------ definition of data set
#include "unit.hpp"
#include "md_enum.hpp"
#include "md_coef_table.hpp"
#include "file_IO_pos.hpp"
#include "file_IO_resume.hpp"
#include "file_Out_VMD.hpp"
#include "atom_class.hpp"
#include "md_setting.hpp"
//------ calculate interaction
#include "md_force.hpp"
//------ system observer
#include "observer.hpp"
//------ external system control
#include "ext_sys_control.hpp"
//------ condition loader
#include "md_loading_condition.hpp"
#include "md_loading_model.hpp"


/**
* @brief main routine
*/
int main(int argc, char* argv[]){
    PS::Initialize(argc, argv);

    //--- record real start time
    auto real_start_time = std::chrono::system_clock::now();

    //--- display total threads for FDPS
    if(PS::Comm::getRank() == 0){
        std::ostringstream oss;
        oss << "Number of processes          : " << PS::Comm::getNumberOfProc()   << "\n"
            << "Number of threads per process: " << PS::Comm::getNumberOfThread() << "\n";
        std::cout << oss.str() << std::flush;
    }


    //--- check for PS::ParticleMesh
    if(PS::Comm::getNumberOfProc() < 2) throw std::logic_error("MPI proc must be >= 2. (for ParticleMesh extension of FDPS)");


    //--- make particle system object
    PS::DomainInfo              dinfo;
    PS::ParticleSystem<Atom_FP> atom;
    CalcForce                   force;

    //--- initialize
    atom.initialize();
    atom.setNumberOfParticleLocal(0);

    //--- make ext_sys controller object
    EXT_SYS::Sequence   ext_sys_sequence;
    EXT_SYS::Controller ext_sys_controller;

    //------ load settings
    if(PS::Comm::getRank() == 0){
        //--- load setting files
        System::loading_sequence_condition(MD_DEFS::condition_sequence_file,
                                           ext_sys_sequence,
                                           ext_sys_controller );
        System::loading_molecular_condition(MD_DEFS::condition_molecule_file);

        System::model_template.resize( System::model_list.size() );

        //--- load model parameters
        for(size_t i=0; i<System::model_list.size(); ++i){
            MODEL::loading_model_parameter(ENUM::what(System::model_list.at(i).first),
                                           System::model_template.at(i),
                                           MODEL::coef_table                          );
        }
        //--- display settings
        Unit::print_unit();
        System::print_profile();
        ext_sys_controller.print();
        ext_sys_sequence.print();
    }

    //--- broadcast settings to all MPI processes
    System::broadcast_profile(0);
    MODEL::coef_table.broadcast(0);
    ext_sys_sequence.broadcast(0);
    ext_sys_controller.broadcast(0);

    //--- load resume file.
    if(System::get_istep() < 0){
        //--- illigal timestep
        std::ostringstream oss;
        oss << " istep = " << System::get_istep() << ", must be >= 0." << "\n";
        throw std::logic_error(oss.str());
    } else {
        //--- load resume file
        FILE_IO::ResumeFileManager init_loader{ MD_DEFS::resume_data_dir };
        init_loader.load(atom, System::profile, ext_sys_controller);

        //--- show statistics value
        if(PS::Comm::getRank() == 0) std::cout << "\n" << std::flush;
        Observer::show_psys_property(atom);
    }

    //--- devide atom particle in MPI processes
    System::InitDinfo(dinfo);
    dinfo.decomposeDomainAll(atom);

    atom.adjustPositionIntoRootDomain(dinfo);
    atom.exchangeParticle(dinfo);

    //--- initialize force culculator
    PS::S64 n_total = atom.getNumberOfParticleGlobal();
    force.init(n_total);

    //--- initialize observer
    Observer::Energy   eng;
    Observer::Property prop;

    Observer::MovingAve<Observer::Energy>   eng_ave;
    Observer::MovingAve<Observer::Property> prop_ave;

    eng.file_init( "energy_raw.dat"  );
    prop.file_init("property_raw.dat");
    eng_ave.file_init( "eng_ave.dat" , System::get_eng_start(),  System::get_eng_interval()  );
    prop_ave.file_init("prop_ave.dat", System::get_prop_start(), System::get_prop_interval() );

    FILE_IO::VMDFileManager    vmd_file_mngr{    MD_DEFS::VMD_data_dir   , System::get_VMD_start()   , System::get_VMD_interval()    };
    FILE_IO::PosFileManager    pos_file_mngr{    MD_DEFS::pos_data_dir   , System::get_pos_start()   , System::get_pos_interval()    };
    FILE_IO::ResumeFileManager resume_file_mngr{ MD_DEFS::resume_data_dir, System::get_resume_start(), System::get_resume_interval() };

    //--- calculate force
    force.update_intra_pair_list(atom, dinfo, MODEL::coef_table.mask_scaling);
    force.update_force(atom, dinfo);


    //--- main loop
    if(PS::Comm::getRank() == 0) std::cout << "\n --- main loop start! ---\n" << std::endl;
    while( System::isLoopContinue() ){

        //--- output record
        vmd_file_mngr.record(   atom, System::profile );
        pos_file_mngr.record(   atom, System::profile );
        resume_file_mngr.record(atom, System::profile, ext_sys_controller);

        //--- get system property
        eng.getEnergy(atom);
        prop.getProperty(eng);

        //--- affect external system controller
        PS::S64 n_rigid = 0;
        ext_sys_controller.apply(n_rigid,
                                 ext_sys_sequence.getSetting( System::get_istep() ),
                                 System::get_dt(),
                                 atom,
                                 eng);

        //--- energy log
        //------ raw data
        eng.record(  System::profile );
        prop.record( System::profile );
        //------ moving average
        eng_ave.record(  System::profile, eng );
        prop_ave.record( System::profile, prop );

        //--- kick
        //ATOM_MOVE::kick(0.5*System::get_dt(), atom);
        ext_sys_controller.kick(0.5*System::get_dt(), atom);

        //--- drift
        ATOM_MOVE::drift(System::get_dt(), atom);
        //ext_sys_controller.drift(System::get_dt(), atom);
        atom.adjustPositionIntoRootDomain(dinfo);

        #ifdef REUSE_INTERACTION_LIST
            //--- update domain info & exchange particle
            if( System::isDinfoUpdate() ){
                dinfo.decomposeDomainAll(atom);
                atom.exchangeParticle(dinfo);

                /*
                if(PS::Comm::getRank() == 0){
                    std::ostringstream oss;
                    oss << "  -- i_step = " << System::get_istep() << ", update dinfo" << "\n";
                    std::cout << oss.str() << std::flush;
                }
                */

                //--- update intra pair list after psys.echangeParticle()
                force.update_intra_pair_list(atom, dinfo, MODEL::coef_table.mask_scaling);

                force.update_force(atom, dinfo, PS::MAKE_LIST_FOR_REUSE);
            } else {
                force.update_force(atom, dinfo, PS::REUSE_LIST);
            }
        #else
            //--- update domain info & exchange particle
            dinfo.decomposeDomainAll(atom);  // perform at every step is requred by PS::ParticleMesh
            atom.exchangeParticle(dinfo);    // perform at every step is requred by PS::ParticleMesh

            //--- calculate intermolecular force in FDPS
            force.update_intra_pair_list(atom, dinfo, MODEL::coef_table.mask_scaling);
            force.update_force(atom, dinfo, PS::MAKE_LIST);
        #endif

        //--- kick
        //ATOM_MOVE::kick(0.5*System::get_dt(), atom);
        ext_sys_controller.kick(0.5*System::get_dt(), atom);

        //--- nest step
        System::StepNext();
    }
    if(PS::Comm::getRank() == 0) std::cout << "\n --- main loop ends! ---\n" << std::endl;

    //--- show elapsed time
    auto real_end_time = std::chrono::system_clock::now();
    if(PS::Comm::getRank() == 0){
        std::cout << "  elapsed time = "
                  << chrono_str::to_str_h_m_s_ms(real_end_time - real_start_time)
                  << "\n" << std::endl;
    }

    //--- finalize FDPS
    PS::Finalize();
    return 0;
}
