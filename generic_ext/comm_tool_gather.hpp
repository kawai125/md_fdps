/**************************************************************************************************/
/**
* @file  comm_tool_gather.hpp
* @brief STL container wrapper for PS::Comm::gather()
*/
/**************************************************************************************************/
#pragma once

#include <cassert>
#include <vector>
#include <string>
#include <tuple>
#include <utility>
#include <unordered_map>

#include <particle_simulator.hpp>

#include "comm_tool_SerDes.hpp"


namespace COMM_TOOL {

    //=====================
    //  wrapper functions
    //=====================

    /**
    * @brief wrapper for static size class.
    * @param[in] value send target. MUST NOT contain pointer member.
    * @param[in] root ID of root process.
    * @param[out] recv_vec recieve result.
    */
    template <class T>
    void gather(const T              &value,
                      std::vector<T> &recv_vec,
                const PS::S32         root,
                      MPI_Comm        comm = MPI_COMM_WORLD){

        #ifdef DEBUG_COMM_TOOL
            if(PS::Comm::getRank() == 0) std::cout << " *** gather_<T, result>" << std::endl;
        #endif

        recv_vec.clear();
        recv_vec.resize(PS::Comm::getNumberOfProc());

        #ifdef PARTICLE_SIMULATOR_MPI_PARALLEL
            MPI_Gather(&value,       1, PS::GetDataType<T>(),
                       &recv_vec[0], 1, PS::GetDataType<T>(),
                        root,
                        comm);
        #else
            recv_vec.at(0) = value;
        #endif
    }

    /**
    * @brief specialization for std::vector<T>.
    * @param[in] send_vec send target. class T MUST NOT contain pointer member.
    * @param[in] root ID of root process.
    * @param[out] recv_vec_vec recieve result.
    */
    template <class T>
    void gather(const std::vector<T>              &send_vec,
                      std::vector<std::vector<T>> &recv_vec_vec,
                const PS::S32                      root,
                      MPI_Comm                     comm = MPI_COMM_WORLD){

        #ifdef DEBUG_COMM_TOOL
            if(PS::Comm::getRank() == 0) std::cout << " *** gather_<std::vector<T>, result>" << std::endl;
        #endif

        std::vector<PS::S32> n_recv;
        std::vector<PS::S32> n_recv_disp;
        std::vector<T>       recv_data;
        PS::S32              len = send_vec.size();
        gather(len, n_recv, root, comm);

        const PS::S32 n_proc = PS::Comm::getNumberOfProc();
        n_recv_disp.resize(n_proc+1);
        n_recv_disp[0] = 0;
        for(PS::S32 i=0; i<n_proc; ++i){
            n_recv_disp.at(i+1) = n_recv_disp.at(i) + n_recv.at(i);
        }
        recv_data.resize( n_recv_disp[n_proc] );

        #ifdef PARTICLE_SIMULATOR_MPI_PARALLEL
            MPI_Gatherv(&send_vec[0],  send_vec.size(),             PS::GetDataType<T>(),
                        &recv_data[0], &n_recv[0], &n_recv_disp[0], PS::GetDataType<T>(),
                         root,
                         comm);
        #else
            recv_data = send_vec;
            n_recv.clear();
            n_recv_disp.clear();
            n_recv      = {send_vec.size()};
            n_recv_disp = {0, send_vec.size()};
        #endif

        if(PS::Comm::getRank() == root){
            recv_vec_vec.resize(n_proc);
            for(PS::S32 i_proc=0; i_proc<n_proc; ++i_proc){
                auto& local_vec = recv_vec_vec.at(i_proc);
                      local_vec.clear();
                      local_vec.reserve(n_recv.at(i_proc));

                PS::S32 index_begin = n_recv_disp.at(i_proc);
                PS::S32 index_end   = index_begin + n_recv.at(i_proc);
                for(PS::S32 index=index_begin; index<index_end; ++index){
                    local_vec.emplace_back( recv_data.at(index) );
                }
            }
        } else {
            recv_vec_vec.clear();
        }
    }

    /**
    * @brief specialization for std::string.
    * @param[in] send_str send target.
    * @param[in] root ID of root process.
    * @param[out] recv_vec_str recieve result.
    */
    void gather(const std::string              &send_str,
                      std::vector<std::string> &recv_vec_str,
                const PS::S32                   root,
                      MPI_Comm                  comm = MPI_COMM_WORLD){

        #ifdef DEBUG_COMM_TOOL
            if(PS::Comm::getRank() == 0 )  std::cout << " *** gather_<std::string, result>" << std::endl;
        #endif

        std::vector<char>              send_vec_char;
        std::vector<std::vector<char>> recv_vec_vec_char;

        const PS::S32 n_proc = PS::Comm::getNumberOfProc();

        serialize_string(send_str, send_vec_char);
        gather(send_vec_char, recv_vec_vec_char, root, comm);

        if(PS::Comm::getRank() == root){
            recv_vec_str.resize(n_proc);
            for(PS::S32 i_proc=0; i_proc<n_proc; ++i_proc){
                deserialize_string(recv_vec_vec_char.at(i_proc),
                                   recv_vec_str.at(i_proc)  );
            }
        } else {
            recv_vec_str.clear();
        }
    }

    /**
    * @brief specialization for std::vector<std::string>.
    * @param[in] send_vec_str send target.
    * @param[in] root ID of root process.
    * @param[out] recv_vec_vec_str recieve result.
    */
    void gather(const std::vector<std::string>              &send_vec_str,
                      std::vector<std::vector<std::string>> &recv_vec_vec_str,
                const PS::S32                                root,
                      MPI_Comm                               comm = MPI_COMM_WORLD){

        #ifdef DEBUG_COMM_TOOL
            if(PS::Comm::getRank() == 0 )  std::cout << " *** gather_<std::vector<std::string>, result>" << std::endl;
        #endif

        std::vector<char>              send_vec_char;
        std::vector<std::vector<char>> recv_vec_vec_char;

        const PS::S32 n_proc = PS::Comm::getNumberOfProc();

        serialize_vector_string(send_vec_str, send_vec_char);
        gather(send_vec_char, recv_vec_vec_char, root, comm);

        if(PS::Comm::getRank() == root){
            recv_vec_vec_str.resize(n_proc);
            for(PS::S32 i_proc=0; i_proc<n_proc; ++i_proc){
                deserialize_vector_string(recv_vec_vec_char.at(i_proc),
                                          recv_vec_vec_str.at(i_proc)  );
            }
        } else {
            recv_vec_vec_str.clear();
        }
    }

    /**
    * @brief specialization for std::vector<std::vector<T>>.
    * @param[in] send_vec_vec send target. class T MUST NOT contain pointer member.
    * @param[in] root ID of root process.
    * @param[out] recv_vec_vec_vec recieve result.
    * @details devide std::vector<std::vector<T>> into std::vector<T> and std::vector<index>, then call allGatherV() for std::vector<T>.
    * @details class T accepts std::vector<>, std::string, or user-defined class WITHOUT pointer member.
    * @details If 3 or more nested std::vector<...> is passed, this function will call itself recurcively.
    */
    template <class T>
    void gather(const std::vector<std::vector<T>>              &send_vec_vec,
                      std::vector<std::vector<std::vector<T>>> &recv_vec_vec_vec,
                const PS::S32                                   root,
                      MPI_Comm                                  comm = MPI_COMM_WORLD){

        #ifdef DEBUG_COMM_TOOL
            if(PS::Comm::getRank() == 0 )  std::cout << " *** gather_<std::vector<std::vector<T>>, result>" << std::endl;
        #endif

        std::vector<T>      vec_data;
        std::vector<size_t> vec_index;

        serialize_vector_vector(send_vec_vec,
                                vec_data, vec_index);

        std::vector<std::vector<T>>      recv_data;
        std::vector<std::vector<size_t>> recv_index;
        gather(vec_data , recv_data , root, comm);
        gather(vec_index, recv_index, root, comm);

        if(PS::Comm::getRank() == root){
            recv_vec_vec_vec.resize(PS::Comm::getNumberOfProc());
            for(PS::S32 i_proc=0; i_proc<PS::Comm::getNumberOfProc(); ++i_proc){
                deserialize_vector_vector(recv_data.at(i_proc), recv_index.at(i_proc),
                                          recv_vec_vec_vec.at(i_proc)                 );
            }
        } else {
            recv_vec_vec_vec.clear();
        }
    }

    /**
    * @brief specialization for std::vector<std::pair<Ta, Tb>>.
    * @param[in] send_vec_pair send target.
    * @param[in] root ID of root process.
    * @param[out] recv_vec_vec_pair recieve result.
    * @details devide std::vector<std::pair<Ta, Tb>> into std::vector<Ta> and std::vector<Tb>, then call allGatherV() for std::vector<T>.
    * @details class Ta and Tb accept std::vector<>, std::string, or user-defined class WITHOUT pointer member.
    */
    template <class Ta, class Tb>
    void gather(const std::vector<std::pair<Ta, Tb>>              &send_vec_pair,
                      std::vector<std::vector<std::pair<Ta, Tb>>> &recv_vec_vec_pair,
                const PS::S32                                      root,
                      MPI_Comm                                     comm = MPI_COMM_WORLD){

        #ifdef DEBUG_COMM_TOOL
            if(PS::Comm::getRank() == 0 )  std::cout << " *** gather_<std::vector<std::pair<Ta, Tb>>, result>" << std::endl;
        #endif

        std::vector<Ta> vec_1st;
        std::vector<Tb> vec_2nd;
        split_vector_pair(send_vec_pair,
                          vec_1st, vec_2nd);

        std::vector<std::vector<Ta>> recv_vec_1st;
        std::vector<std::vector<Tb>> recv_vec_2nd;
        gather(vec_1st, recv_vec_1st, root, comm);
        gather(vec_2nd, recv_vec_2nd, root, comm);

        if(PS::Comm::getRank() == root){
            recv_vec_vec_pair.resize(PS::Comm::getNumberOfProc());
            for(PS::S32 i=0; i<PS::Comm::getNumberOfProc(); ++i){
                combine_vector_pair(recv_vec_1st.at(i), recv_vec_2nd.at(i),
                                    recv_vec_vec_pair.at(i)                );
            }
        } else {
            recv_vec_vec_pair.clear();
        }
    }


    /**
    * @brief specialization for returning type interface.
    * @param[in] send_value send target.
    * @param[in] root ID of root process.
    * @return    recieve_vector recieved data.
    * @detail    wrapper for "auto recv_vec = COMM_TOOL::allGather(data);"
    * @detail    size of vector = PS::Comm::getNumberOfProc();
    */
    template <class T>
    std::vector<T> gather(const T       &send_data,
                          const PS::S32  root,
                                MPI_Comm comm = MPI_COMM_WORLD){
        std::vector<T> recv_vec;
        gather(send_data, recv_vec, root, comm);
        return recv_vec;
    }

}
