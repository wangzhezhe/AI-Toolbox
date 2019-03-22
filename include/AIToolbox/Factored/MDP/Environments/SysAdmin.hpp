#ifndef AI_TOOLBOX_FACTORED_MDP_MULTI_AGENT_SYS_ADMIN
#define AI_TOOLBOX_FACTORED_MDP_MULTI_AGENT_SYS_ADMIN

#include <AIToolbox/Factored/MDP/CooperativeModel.hpp>

namespace AIToolbox::Factored::MDP {
    namespace SysAdminEnums {
        enum MachineStatus {
            Good = 0,
            Fail,
            Dead
        };

        enum MachineLoad {
            Idle = 0,
            Load,
            Done
        };
    }

    /**
     * @brief This function creates a ring where each machine affects only the next adjacent one.
     *
     * Note that pFailBonus and pDeadBonus are the total additional bonuses
     * counted when all neighbors are faulty/dead, respectively. However, the
     * bonuses are counted per-agent.
     *
     * If a machine with 2 neighbors has a single faulty neighbor, it will get
     * an additional failing probability of `pFailBonus/2`. If the same machine
     * has one faulty neighbor and one dead neighbor, it will get a penalty of
     * `pFailBonus/2 + pDeadBonus/2`.
     *
     * @param agents The number of agents in the ring.
     * @param pFailBase The base probability of a machine to fail.
     * @param pFailBonus The total additional probability to fail/die when all neighbors are faulty (counted per-neighbor).
     * @param pDeadBase The base probability of a faulty machine to die.
     * @param pDeadBonus The total additional probability to fail/die when all neighbors are dead (counted per-neighbor).
     * @param pLoad The probability of getting a job when idle.
     * @param pDoneG The probability of completing a job when good.
     * @param pDoneF The probability of completing a job when faulty.
     *
     * @return The CooperativeModel representing the problem.
     */
    CooperativeModel makeSysAdminUniRing(unsigned agents,
        // Status transition params.
        double pFailBase, double pFailBonus, double pDeadBase, double pDeadBonus,
        // Load transition params.
        double pLoad, double pDoneG, double pDoneF);

    /**
     * @brief This function creates a ring where each machine affects the two adjacent ones.
     *
     * Note that pFailBonus and pDeadBonus are the total additional bonuses
     * counted when all neighbors are faulty/dead, respectively. However, the
     * bonuses are counted per-agent.
     *
     * If a machine with 2 neighbors has a single faulty neighbor, it will get
     * an additional failing probability of `pFailBonus/2`. If the same machine
     * has one faulty neighbor and one dead neighbor, it will get a penalty of
     * `pFailBonus/2 + pDeadBonus/2`.
     *
     * @param agents The number of agents in the ring.
     * @param pFailBase The base probability of a machine to fail.
     * @param pFailBonus The total additional probability to fail/die when all neighbors are faulty (counted per-neighbor).
     * @param pDeadBase The base probability of a faulty machine to die.
     * @param pDeadBonus The total additional probability to fail/die when all neighbors are dead (counted per-neighbor).
     * @param pLoad The probability of getting a job when idle.
     * @param pDoneG The probability of completing a job when good.
     * @param pDoneF The probability of completing a job when faulty.
     *
     * @return The CooperativeModel representing the problem.
     */
    CooperativeModel makeSysAdminBiRing(unsigned agents,
        // Status transition params.
        double pFailBase, double pFailBonus, double pDeadBase, double pDeadBonus,
        // Load transition params.
        double pLoad, double pDoneG, double pDoneF);

    /**
     * @brief This function creates a graphical representation of a SysAdmin ring problem.
     *
     * Each agent is represented with 2 characters: the first represents the
     * Status ('g'ood, 'f'aulty, 'd'ead), and the second represents the Load
     * ('i'dle, 'l'oaded, 'd'one).
     *
     * @param s The State to represent.
     *
     * @return A graphical representation to print on screen.
     */
    std::string printSysAdminRing(const State & s);
}

#endif
