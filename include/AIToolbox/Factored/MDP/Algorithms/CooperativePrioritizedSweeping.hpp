#ifndef AI_TOOLBOX_FACTORED_MDP_COOPERATIVE_PRIORITIZED_SWEEPING_HEADER_FILE
#define AI_TOOLBOX_FACTORED_MDP_COOPERATIVE_PRIORITIZED_SWEEPING_HEADER_FILE

#include <AIToolbox/Factored/MDP/Types.hpp>
#include <AIToolbox/Factored/Utils/Core.hpp>
#include <AIToolbox/Factored/Utils/FactoredMatrix.hpp>
#include <AIToolbox/Factored/MDP/Policies/QGreedyPolicy.hpp>
#include <AIToolbox/Impl/Seeder.hpp>

#include <boost/functional/hash.hpp>
#include <boost/heap/fibonacci_heap.hpp>

#include <iostream>

namespace AIToolbox::Factored::MDP {
    template <typename T>
    std::ostream & operator<<(std::ostream & os, const std::vector<T> & v) {
        for (auto vv : v)
            os << vv << ' ';
        return os;
    }
    std::ostream & operator<<(std::ostream & os, const PartialFactors & pf) {
        os << pf.first << " ==> " << pf.second;
        return os;
    }
    template <typename M>
    class CooperativePrioritizedSweeping {
        public:
            CooperativePrioritizedSweeping(const M & m, std::vector<std::vector<size_t>> basisDomains, double alpha = 0.3, double theta = 0.001);

            void stepUpdateQ(const State & s, const Action & a, const State & s1, const Rewards & r);

            void batchUpdateQ(const unsigned N = 50);

            const FactoredMatrix2D & getQFunction() const;

        private:
            void updateQ(const State & s, const Action & a, const State & s1, const Rewards & r);

            void addToQueue(const State & s1);

            const M & model_;

            double alpha_, theta_;

            std::vector<std::vector<size_t>> qDomains_;
            Vector rewardWeights_, deltaStorage_;

            FactoredMatrix2D q_;

            QGreedyPolicy gp_;

            using Backup = PartialFactors;

            struct PriorityQueueElement {
                double priority;
                size_t id;
                Backup stateAction;
                bool operator<(const PriorityQueueElement& arg2) const {
                    return priority < arg2.priority;
                }
            };

            using QueueType = boost::heap::fibonacci_heap<PriorityQueueElement>;

            QueueType queue_;
            Trie ids_;
            std::unordered_map<size_t, typename QueueType::handle_type> findById_;
            std::unordered_map<Backup, typename QueueType::handle_type, boost::hash<Backup>> findByBackup_;

            mutable RandomEngine rand_;
    };

    template <typename M>
    CooperativePrioritizedSweeping<M>::CooperativePrioritizedSweeping(const M & m, std::vector<std::vector<size_t>> basisDomains, double alpha, double theta) :
            model_(m),
            alpha_(alpha), theta_(theta),
            qDomains_(std::move(basisDomains)),
            rewardWeights_(model_.getS().size()),
            deltaStorage_(model_.getS().size()),
            gp_(model_.getS(), model_.getA(), q_),
            ids_(join(model_.getS(), model_.getA())),
            rand_(Impl::Seeder::getSeed())
    {
        const auto & ddn = model_.getTransitionFunction();

        // Note that unused reward weights might result in r/0 or 0/0
        // operations, but since then we won't be using those elements
        // anyway it's not a problem.
        rewardWeights_.setZero();

        q_.bases.reserve(qDomains_.size());
        for (const auto & domain : qDomains_) {
            q_.bases.emplace_back();
            auto & fm = q_.bases.back();

            for (auto d : domain) {
                // Note that there's one more Q factor that depends
                // this state factor.
                rewardWeights_[d] += 1.0;

                // Compute state-action domain for this Q factor.
                fm.actionTag = merge(fm.actionTag, ddn[d].actionTag);
                for (const auto & n : ddn[d].nodes)
                    fm.tag = merge(fm.tag, n.tag);
            }

            // Initialize this factor's matrix.
            const size_t sizeA = factorSpacePartial(fm.actionTag, model_.getA());
            const size_t sizeS = factorSpacePartial(fm.tag, model_.getS());

            fm.values.resize(sizeS, sizeA);
            fm.values.setZero();
        }
        //std::cout << "Rewards weights: " << rewardWeights_.transpose() << '\n';
    }

    template <typename M>
    void CooperativePrioritizedSweeping<M>::stepUpdateQ(const State & s, const Action & a, const State & s1, const Rewards & r) {
        //std::cout << "Running new stepUpdateQ with:\n"
        //              "- s  = " << s  << '\n' <<
        //              "- a  = " << a  << '\n' <<
        //              "- s1 = " << s1 << '\n' <<
        //              "- r  = " << r.transpose()  << '\n';
        updateQ(s, a, s1, r.array() / rewardWeights_.array());
        addToQueue(s);
    }

    template <typename M>
    void CooperativePrioritizedSweeping<M>::batchUpdateQ(const unsigned N) {
        for (size_t n = 0; n < N; ++n) {
            if (queue_.empty()) return;

            // Pick top element from queue
            auto [priority, id, stateAction] = queue_.top();

            queue_.pop();

            ids_.erase(id);
            findById_.erase(id);
            findByBackup_.erase(stateAction);

            //std::cout << "BATCH UPDATE\n";
            //std::cout << "Selected initial SA: " << stateAction << '\n';

            // We want to remove as many rules in one swoop as possible, thus
            // we take all rules compatible with our initial pick.
            auto ids = ids_.filter(stateAction);
            while (ids.size()) {
                // Take a random compatible element to add to the first
                // one. Ideally one would want to pick the one with the
                // highest priority, but it's also very important to be as
                // fast as possible here since we want to do as many
                // updates as we can; thus, we do the easiest thing.
                id = ids.back();
                //std::cout << "Extracted additional index " << id << '\n';
                ids.pop_back();

                // Find the handle to the backup in the priority queue.
                auto hIt = findById_.find(id);
                assert(hIt != std::end(findById_));

                // Here is the new piece we want to add to our stateAction
                auto handle = hIt->second;
                auto & newSA = (*handle).stateAction;

                // Cleanup unordered_maps now before we touch anything.
                findById_.erase(hIt);
                findByBackup_.erase(newSA);

                // Now, we want to make this piece as small as possible, since
                // the refine operation does an amount of work proportional to
                // the length of the input passed.
                // Thus, we remove all common elements between stateAction and newSA.
                {
                    size_t i = 0, j = 0;
                    while (i < newSA.first.size() && j < stateAction.first.size()) {
                        if (newSA.first[i] < stateAction.first[j]) {
                            ++i;
                        } else if (newSA.first[i] > stateAction.first[j]) {
                            ++j;
                        } else {
                            newSA.first.erase(std::begin(newSA.first) + i);
                            newSA.second.erase(std::begin(newSA.second) + i);
                            // we don't update i since we just removed it.
                            ++j;
                        }
                    }
                }

                // Update ids and re-filter with shortest newSA.
                ids_.erase(id);
                ids = ids_.refine(ids, newSA);

                // Add the selected state-action pair and add it to our own.
                // Note that the "pruning" before does not change this result.
                stateAction = merge(stateAction, newSA);
                //std::cout << "Merged SA: " << stateAction << '\n';

                // Finally, clear the element from the queue (which should also
                // kill newSA).
                queue_.erase(handle);
            }

            //std::cout << "Done merging: " << stateAction << "\n";

            std::vector<size_t> missingS;
            std::vector<size_t> missingA;

            State s(model_.getS().size());
            Action a(model_.getA().size());

            // Copy stateAction values to s and a, and record missing ids.
            size_t x = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                if (x >= stateAction.first.size() || i < stateAction.first[x])
                    missingS.push_back(i);
                else
                    s[i] = stateAction.second[x++];
            }

            //std::cout << "S: " << s << " ; missingS: " << missingS << " ; x = " << x << '\n';

            for (size_t i = 0; i < a.size(); ++i) {
                if (x >= stateAction.first.size() || i + model_.getS().size() < stateAction.first[x])
                    missingA.push_back(i);
                else
                    a[i] = stateAction.second[x++];
            }

            //std::cout << "A: " << a << " ; missingA: " << missingA << '\n';

            for (auto ss : missingS) {
                std::uniform_int_distribution<size_t> dist(0, model_.getS()[ss]-1);
                s[ss] = dist(rand_);
            }

            for (auto aa : missingA) {
                std::uniform_int_distribution<size_t> dist(0, model_.getA()[aa]-1);
                a[aa] = dist(rand_);
            }

            //std::cout << "Final S: " << s << " ; final A: " << a << '\n';

            const auto [s1, r] = model_.sampleSRs(s, a);

            updateQ(s, a, s1, r.array() / rewardWeights_.array());
            // Since adding to queue is a relatively expensive operation, we
            // only update it once in a while. Here we update it if the
            // priority of the max element we have just popped off the queue is
            // lower than the current max update.
            //
            // If this is not called, each updateQ accumulates its changes to
            // the deltaStorage_.
            if (deltaStorage_.maxCoeff() > priority)
                addToQueue(s);

            // PartialFactorsEnumerator e(model_.getA(), missingA);
            // while (e.isValid()) {
            //     // Set missing actions.
            //     for (size_t i = 0; i < missingA.size(); ++i)
            //         a[e->first[i]] = a[e->second[i]];

            //     const auto [s1, r] = model_.sampleSR(s, a);
            //     updateQ(s, a, s1, r.array() / rewardWeights_.array());
            // }
        }
    }

    template <typename M>
    const FactoredMatrix2D & CooperativePrioritizedSweeping<M>::getQFunction() const {
        return q_;
    }

    template <typename M>
    void CooperativePrioritizedSweeping<M>::updateQ(const State & s, const Action & a, const State & s1, const Rewards & r) {
        // Compute optimal action to do Q-Learning update.
        const auto a1 = gp_.sampleAction(s1);

        // We update each Q factor separately.
        for (size_t i = 0; i < q_.bases.size(); ++i) {
            auto & q = q_.bases[i];

            const auto sid = toIndexPartial(q.tag, model_.getS(), s);
            const auto aid = toIndexPartial(q.actionTag, model_.getA(), a);

            const auto s1id = toIndexPartial(q.tag, model_.getS(), s1);
            const auto a1id = toIndexPartial(q.actionTag, model_.getA(), a1);

            // Compute numerical reward from the components children of this Q
            // factor.
            double rr = 0.0;
            for (auto s : qDomains_[i])
                rr += r[s]; // already divided by weights

            const auto originalQ = q.values(sid, aid);

            // Q-Learning update
            q.values(sid, aid) += alpha_ * ( rr + model_.getDiscount() * q.values(s1id, a1id) - q.values(sid, aid) );

            //std::cout << (std::fabs(originalQ - q.values(sid, aid)) / q.tag.size()) << ", ";

            // Split the delta to each element referenced by this Q factor.
            // Note that we add to the storage, which is only cleared once we
            // call addToQueue; this means that multiple calls to this
            // functions cumulate their deltas.
            for (auto s : q.tag)
                deltaStorage_[s] += std::fabs(originalQ - q.values(sid, aid)) / q.tag.size();
;
        }
        //std::cout << '\n';
        //std::cout << "Final deltas per-state: " << deltasNoV << '\n';
    }

    template <typename M>
    void CooperativePrioritizedSweeping<M>::addToQueue(const State & s1) {
        // Note that s1 was s before, but here we consider it as the
        // "future" state as we look for its parents.

        const auto & T = model_.getTransitionFunction();

        for (size_t i = 0; i < s1.size(); ++i) {
            const auto & aNode = T.nodes[i];
            for (size_t a = 0; a < aNode.nodes.size(); ++a) {
                const auto & sNode = aNode.nodes[a];
                for (size_t s = 0; s < static_cast<size_t>(sNode.matrix.rows()); ++s) {
                    const auto p = sNode.matrix(s, s1[i]) * deltaStorage_[i];

                    if (p < theta_) continue;

                    Backup backup = PartialFactors{
                        join(model_.getS().size(), sNode.tag, aNode.actionTag), // Keys
                        join(                             // Values
                                toFactorsPartial(sNode.tag,       model_.getS(), s),
                                toFactorsPartial(aNode.actionTag, model_.getA(), a)
                            )
                    };
                    auto hIt = findByBackup_.find(backup);

                    if (hIt != std::end(findByBackup_)) {
                        auto handle = hIt->second;

                        (*handle).priority += p;
                        queue_.increase(handle);
                    } else {
                        auto id = ids_.insert(backup);
                        auto handle = queue_.emplace(PriorityQueueElement{p, id, backup});

                        //std::cout << "Inserted in IDS [" << backup << "] with index " << id << '\n';
                        //std::cout << "    Value in queue: " << (*handle).stateAction << '\n';

                        findById_[id] = handle;
                        findByBackup_[backup] = handle;
                    }
                }
            }
        }
        deltaStorage_.setZero();

        //std::cout << "Queue now contains " << queue_.size() << " entries.\n";
    }
}

#endif
