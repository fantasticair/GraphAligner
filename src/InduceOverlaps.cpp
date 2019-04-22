#include <mutex>
#include <concurrentqueue.h>
#include <thread>
#include <iostream>
#include "Assemble.h"

template <typename F>
void iterateMostRecentAncestors(std::unordered_set<size_t>& tips, const std::vector<std::vector<size_t>>& inNeighbors, F f)
{
	std::unordered_set<size_t> visited;
	std::vector<size_t> visitStack;
	for (auto tip : tips)
	{
		if (!f(tip))
		{
			for (auto neighbor : inNeighbors[tip])
			{
				visitStack.push_back(neighbor);
			}
		}
	}
	while (visitStack.size() > 0)
	{
		size_t k = visitStack.back();
		visitStack.pop_back();
		if (visited.count(k) == 1) return;
		visited.insert(k);
		if (!f(k))
		{
			for (auto neighbor : inNeighbors[k])
			{
				visitStack.push_back(neighbor);
			}
		}
	}
}

Alignment alignSparse(const Path& leftPath, const Path& rightPath, const std::unordered_map<int, size_t>& nodeSizes, const std::vector<size_t>& leftCumulativePrefixLength, const std::vector<size_t>& rightCumulativePrefixLength, const std::unordered_map<NodePos, std::vector<size_t>>& rightOccurrences, size_t left, size_t right, double mismatchPenalty)
{
	std::vector<std::tuple<size_t, size_t, double, size_t>> trace;
	std::unordered_set<size_t> tips;
	std::vector<std::vector<size_t>> inNeighbors;
	size_t bestTraceStart = -1;
	double bestStartScore = 0;
	for (size_t i = 0; i < leftPath.position.size(); i++)
	{
		if (rightOccurrences.count(leftPath.position[i]) == 1)
		{
			for (auto j : rightOccurrences.at(leftPath.position[i]))
			{
				size_t index = trace.size();
				tips.insert(index);
				inNeighbors.emplace_back();
				trace.emplace_back(i, j, 0, (size_t)-1);
				std::get<2>(trace.back()) = -(double)std::min(leftCumulativePrefixLength[i], rightCumulativePrefixLength[j]) * mismatchPenalty;
				assert(std::get<2>(trace.back()) < leftCumulativePrefixLength.back() && std::get<2>(trace.back()) < rightCumulativePrefixLength.back());
				assert(std::get<2>(trace.back()) > -(double)leftCumulativePrefixLength.back() * mismatchPenalty || std::get<2>(trace.back()) > -(double)rightCumulativePrefixLength.back() * mismatchPenalty);
				std::vector<size_t> removedTips;
				iterateMostRecentAncestors(tips, inNeighbors, [mismatchPenalty, &leftCumulativePrefixLength, &rightCumulativePrefixLength, &leftPath, &rightPath, &trace, &inNeighbors, &removedTips, i, j, index](size_t k)
				{
					if (k == index) return false;
					if (std::get<0>(trace[k]) >= i || std::get<1>(trace[k]) >= j) return false;
					removedTips.push_back(k);
					inNeighbors[index].push_back(k);
					double insertions = leftCumulativePrefixLength[i] - leftCumulativePrefixLength[std::get<0>(trace[k])+1];
					double deletions = rightCumulativePrefixLength[j] - rightCumulativePrefixLength[std::get<1>(trace[k])+1];
					assert(insertions >= 0);
					assert(deletions >= 0);
					assert(insertions <= leftCumulativePrefixLength.back());
					assert(deletions <= rightCumulativePrefixLength.back());
					double btScore = std::get<2>(trace[k]) - std::max(insertions, deletions) * mismatchPenalty;
					if (btScore > std::get<2>(trace.back()))
					{
						std::get<2>(trace.back()) = btScore;
						std::get<3>(trace.back()) = k;
					}
					return true;
				});
				for (auto k : removedTips)
				{
					tips.erase(k);
				}
				assert(leftPath.position[i] == rightPath.position[j]);
				std::get<2>(trace.back()) += nodeSizes.at(leftPath.position[i].id);
				double startScoreHere = std::get<2>(trace.back()) - (double)std::min(leftCumulativePrefixLength.back() - leftCumulativePrefixLength[i+1], rightCumulativePrefixLength.back() - rightCumulativePrefixLength[j+1]) * mismatchPenalty;
				if (startScoreHere > bestStartScore || bestTraceStart == -1)
				{
					bestTraceStart = trace.size()-1;
					bestStartScore = startScoreHere;
				}
			}
		}
	}
	Alignment result;
	if (trace.size() == 0)
	{
		result.alignmentIdentity = 0;
		return result;
	}
	size_t pos = bestTraceStart;
	size_t i = std::get<0>(trace[pos]);
	size_t j = std::get<1>(trace[pos]);
	size_t matchLen = 0;
	size_t mismatchLen = 0;
	size_t indelSize = std::min(leftCumulativePrefixLength.back() - leftCumulativePrefixLength[i+1], rightCumulativePrefixLength.back() - rightCumulativePrefixLength[j+1]);
	assert(indelSize >= 0);
	assert(indelSize <= leftCumulativePrefixLength.back() || indelSize <= rightCumulativePrefixLength.back());
	result.leftPath = left;
	result.rightPath = right;
	result.leftEnd = i;
	result.rightEnd = j;
	assert(result.leftEnd < leftPath.position.size());
	assert(result.rightEnd < rightPath.position.size());
	// one past the real end
	while (result.leftEnd < leftPath.position.size() && leftCumulativePrefixLength[result.leftEnd+1] - leftCumulativePrefixLength[i+1] <= indelSize) result.leftEnd++;
	while (result.rightEnd < rightPath.position.size() && rightCumulativePrefixLength[result.rightEnd+1] - rightCumulativePrefixLength[j+1] <= indelSize) result.rightEnd++;
	// fix to correct position
	result.leftEnd--;
	result.rightEnd--;
	assert(result.leftEnd == leftPath.position.size()-1 || result.rightEnd == rightPath.position.size()-1);
	mismatchLen = indelSize;
	while (std::get<3>(trace[pos]) != (size_t)-1)
	{
		assert(std::get<3>(trace[pos]) < pos);
		i = std::get<0>(trace[pos]);
		j = std::get<1>(trace[pos]);
		matchLen += nodeSizes.at(leftPath.position[i].id);
		result.alignedPairs.emplace_back();
		result.alignedPairs.back().leftIndex = i;
		result.alignedPairs.back().rightIndex = j;
		pos = std::get<3>(trace[pos]);
		size_t nextI = std::get<0>(trace[pos]);
		size_t nextJ = std::get<1>(trace[pos]);
		indelSize = std::max(leftCumulativePrefixLength[i] - leftCumulativePrefixLength[nextI+1], rightCumulativePrefixLength[j] - rightCumulativePrefixLength[nextJ+1]);
		assert(indelSize >= 0);
		assert(indelSize <= leftCumulativePrefixLength.back() || indelSize <= rightCumulativePrefixLength.back());
		mismatchLen += indelSize;
	}
	i = std::get<0>(trace[pos]);
	j = std::get<1>(trace[pos]);
	result.alignedPairs.emplace_back();
	result.alignedPairs.back().leftIndex = i;
	result.alignedPairs.back().rightIndex = j;
	matchLen += nodeSizes.at(leftPath.position[i].id);
	indelSize = std::min(leftCumulativePrefixLength[i], rightCumulativePrefixLength[j]);
	assert(indelSize >= 0);
	assert(indelSize <= leftCumulativePrefixLength.back() || indelSize <= rightCumulativePrefixLength.back());
	mismatchLen += indelSize;
	result.leftStart = i;
	result.rightStart = j;
	while (result.leftStart > 0 && leftCumulativePrefixLength[i] - leftCumulativePrefixLength[result.leftStart-1] <= indelSize) result.leftStart--;
	while (result.rightStart > 0 && rightCumulativePrefixLength[j] - rightCumulativePrefixLength[result.rightStart-1] <= indelSize) result.rightStart--;
	assert(result.leftStart == 0 || result.rightStart == 0);
	result.alignmentLength = matchLen + mismatchLen;
	result.alignmentIdentity = (double)matchLen / ((double)mismatchLen + (double)matchLen);
	return result;
}

Alignment align(const std::vector<NodePos>& leftPath, const std::vector<NodePos>& rightPath, const std::unordered_map<int, size_t>& nodeSizes, size_t left, size_t right, double mismatchPenalty)
{
	enum BacktraceType
	{
		Insertion,
		Deletion,
		Match,
		Mismatch,
		Start
	};
	static thread_local std::vector<std::vector<double>> DPscores;
	static thread_local std::vector<std::vector<BacktraceType>> DPtrace;
	static thread_local std::vector<std::vector<size_t>> matches;
	if (DPscores.size() < leftPath.size()+1) DPscores.resize(leftPath.size()+1);
	if (DPtrace.size() < leftPath.size()+1) DPtrace.resize(leftPath.size()+1);
	if (matches.size() < leftPath.size()+1) matches.resize(leftPath.size()+1);
	if (DPscores.back().size() < rightPath.size()+1)
	{
		for (size_t i = 0; i < DPscores.size(); i++)
		{
			DPscores[i].resize(rightPath.size()+1, 0);
			DPtrace[i].resize(rightPath.size()+1, Start);
			matches[i].resize(rightPath.size()+1, 0);
		}
	}
	size_t maxI = 0;
	size_t maxJ = 0;
	for (size_t i = 0; i < leftPath.size(); i++)
	{
		for (size_t j = 0; j < rightPath.size(); j++)
		{
			matches[i+1][j+1] = 0;
			DPscores[i+1][j+1] = 0;
			DPtrace[i+1][j+1] = Start;
			bool match = (leftPath[i] == rightPath[j]);
			size_t leftSize = nodeSizes.at(leftPath[i].id);
			size_t rightSize = nodeSizes.at(rightPath[j].id);
			double insertionCost = leftSize * mismatchPenalty;
			double deletionCost = rightSize * mismatchPenalty;
			double mismatchCost = std::max(insertionCost, deletionCost);
			double matchScore = leftSize;
			assert(!match || leftSize == rightSize);
			// if (DPscores[i][j+1] - insertionCost > DPscores[i+1][j+1])
			// {
				DPscores[i+1][j+1] = DPscores[i][j+1] - insertionCost;
				DPtrace[i+1][j+1] = Insertion;
				matches[i+1][j+1] = matches[i][j+1];
			// }
			if (DPscores[i+1][j] - deletionCost > DPscores[i+1][j+1])
			{
				DPscores[i+1][j+1] = DPscores[i+1][j] - deletionCost;
				DPtrace[i+1][j+1] = Deletion;
				matches[i+1][j+1] = matches[i+1][j];
			}
			if (match && DPscores[i][j] + matchScore >= DPscores[i+1][j+1])
			{
				DPscores[i+1][j+1] = DPscores[i][j] + matchScore;
				DPtrace[i+1][j+1] = Match;
				// maxima.erase(std::make_pair(i, j));
				// maxima.emplace(i+1, j+1);
				matches[i+1][j+1] = matches[i][j] + matchScore;
			}
			if (!match && DPscores[i][j] - mismatchCost >= DPscores[i+1][j+1])
			{
				DPscores[i+1][j+1] = DPscores[i][j] - mismatchCost;
				DPtrace[i+1][j+1] = Mismatch;
				matches[i+1][j+1] = matches[i][j];
			}
			if ((i == leftPath.size()-1 || j == rightPath.size() - 1) && DPscores[i+1][j+1] >= DPscores[maxI][maxJ])
			{
				maxI = i+1;
				maxJ = j+1;
			}
		}
	}
	Alignment result;
	if (maxI == 0 && maxJ == 0)
	{
		result.alignmentIdentity = 0;
		return result;
	}
	size_t matchLen = 0;
	size_t mismatchLen = 0;
	result.leftPath = left;
	result.rightPath = right;
	result.alignmentLength = 0;
	result.leftEnd = maxI-1;
	result.rightEnd = maxJ-1;
	while (DPtrace[maxI][maxJ] != Start)
	{
		assert(maxI > 0);
		assert(maxJ > 0);
		size_t leftSize = nodeSizes.at(leftPath[maxI-1].id);
		size_t rightSize = nodeSizes.at(rightPath[maxJ-1].id);
		result.leftStart = maxI-1;
		result.rightStart = maxJ-1;
		switch(DPtrace[maxI][maxJ])
		{
			case Insertion:
				mismatchLen += leftSize;
				maxI -= 1;
				continue;
			case Deletion:
				mismatchLen += rightSize;
				maxJ -= 1;
				continue;
			case Match:
				assert(leftSize == rightSize);
				result.alignedPairs.emplace_back();
				result.alignedPairs.back().leftIndex = maxI-1;
				result.alignedPairs.back().rightIndex = maxJ-1;
				matchLen += leftSize;
				maxI -= 1;
				maxJ -= 1;
				continue;
			case Mismatch:
				mismatchLen += std::max(leftSize, rightSize);
				maxI -= 1;
				maxJ -= 1;
				continue;
			case Start:
			default:
				assert(false);
		}
	}
	result.alignmentLength = matchLen + mismatchLen;
	if (result.alignmentLength == 0)
	{
		result.alignmentIdentity = 0;
	}
	else
	{
		result.alignmentIdentity = (double)matchLen / ((double)matchLen + (double)mismatchLen);
	}
	return result;
}

std::vector<size_t> getCumulativePrefixLength(const Path& path, const std::unordered_map<int, size_t>& nodeSizes)
{
	std::vector<size_t> result;
	result.resize(path.position.size()+1, 0);
	for (int i = 0; i < path.position.size(); i++)
	{
		result[i+1] = result[i] + nodeSizes.at(path.position[i].id);
	}
	return result;
}

std::unordered_map<NodePos, std::vector<size_t>> getOccurrences(const Path& path)
{
	std::unordered_map<NodePos, std::vector<size_t>> result;
	for (size_t i = 0; i < path.position.size(); i++)
	{
		result[path.position[i]].push_back(i);
	}
	return result;
}

void induceOverlaps(const std::vector<Path>& paths, const std::unordered_map<int, size_t>& nodeSizes, double mismatchPenalty, size_t minAlnLength, double minAlnIdentity, int numThreads, std::string tempAlnFileName)
{
	std::unordered_map<NodePos, std::vector<size_t>> crossesNode;
	for (size_t i = 0; i < paths.size(); i++)
	{
		for (auto node : paths[i].position)
		{
			crossesNode[node].push_back(i);
		}
	}
	std::vector<std::vector<size_t>> cumulativePrefixLengths;
	cumulativePrefixLengths.resize(paths.size());
	for (size_t i = 0; i < paths.size(); i++)
	{
		cumulativePrefixLengths[i] = getCumulativePrefixLength(paths[i], nodeSizes);
	}
	moodycamel::ConcurrentQueue<Alignment> writequeue;
	std::atomic<bool> overlapsFinished;
	overlapsFinished = false;
	size_t alnCount = 0;
	std::thread overlapWriter { [&alnCount, tempAlnFileName, &overlapsFinished, &writequeue](){
		std::ofstream outfile { tempAlnFileName, std::ios::out | std::ios::binary };
		while (true)
		{
			Alignment alns[100] {};
			size_t gotOverlaps = writequeue.try_dequeue_bulk(alns, 100);
			if (gotOverlaps == 0)
			{
				if (!writequeue.try_dequeue(alns[0]))
				{
					if (overlapsFinished) break;
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}
				gotOverlaps = 1;
			}
			for (size_t i = 0; i < gotOverlaps; i++)
			{
				WriteAlignment(outfile, alns[i]);
			}
			alnCount += gotOverlaps;
		}
	}};
	std::vector<std::thread> threads;
	std::mutex nextReadMutex;
	size_t nextRead = 0;
	for (size_t thread = 0; thread < numThreads; thread++)
	{
		threads.emplace_back([&writequeue, &nodeSizes, &cumulativePrefixLengths, &paths, &nextRead, &nextReadMutex, thread, &crossesNode, minAlnIdentity, minAlnLength, mismatchPenalty]()
		{
			while (true)
			{
				size_t i = 0;
				{
					std::lock_guard<std::mutex> guard { nextReadMutex };
					i = nextRead;
					nextRead += 1;
				}
				if (i >= paths.size()) break;
				std::cerr << i << "/" << paths.size() << std::endl;
				std::unordered_map<size_t, size_t> possibleFwMatches;
				std::unordered_map<size_t, size_t> possibleBwMatches;
				auto reversePath = paths[i].Reverse();
				auto occurrences = getOccurrences(paths[i]);
				auto reverseOccurrences = getOccurrences(reversePath);
				auto reverseCumulativePrefixLengths = getCumulativePrefixLength(reversePath, nodeSizes);
				for (size_t j = 0; j < paths[i].position.size(); j++)
				{
					auto node = paths[i].position[j];
					size_t nodeSize = nodeSizes.at(paths[i].position[j].id);
					for (auto other : crossesNode[node])
					{
						if (other <= i) continue;
						possibleFwMatches[other] += nodeSize;
					}
				}
				for (size_t j = 0; j < reversePath.position.size(); j++)
				{
					auto node = reversePath.position[j];
					size_t nodeSize = nodeSizes.at(reversePath.position[j].id);
					for (auto other : crossesNode[node])
					{
						if (other <= i) continue;
						possibleBwMatches[other] += nodeSize;
					}
				}
				for (auto pair : possibleFwMatches)
				{
					size_t j = pair.first;
					if (pair.second < minAlnLength) continue;
					if (i == j) continue;
					Alignment fwAln;
					if (pair.second > cumulativePrefixLengths[i].back() || pair.second > cumulativePrefixLengths[j].back())
					{
						fwAln = align(paths[j].position, paths[i].position, nodeSizes, j, i, mismatchPenalty);
					}
					else
					{
						fwAln = alignSparse(paths[j], paths[i], nodeSizes, cumulativePrefixLengths[j], cumulativePrefixLengths[i], occurrences, j, i, mismatchPenalty);
					}
					if (fwAln.alignmentLength >= minAlnLength && fwAln.alignmentIdentity >= minAlnIdentity)
					{
						for (size_t k = 0; k < fwAln.alignedPairs.size(); k++)
						{
							fwAln.alignedPairs[k].leftReverse = false;
							fwAln.alignedPairs[k].rightReverse = false;
						}
						fwAln.rightReverse = false;
						writequeue.enqueue(fwAln);
					}
				}
				for (auto pair : possibleBwMatches)
				{
					size_t j = pair.first;
					if (pair.second < minAlnLength) continue;
					if (i == j) continue;
					Alignment bwAln;
					if (pair.second > cumulativePrefixLengths[i].back() || pair.second > cumulativePrefixLengths[j].back())
					{
						bwAln = align(paths[j].position, reversePath.position, nodeSizes, j, i, mismatchPenalty);
					}
					else
					{
						bwAln = alignSparse(paths[j], reversePath, nodeSizes, cumulativePrefixLengths[j], reverseCumulativePrefixLengths, reverseOccurrences, j, i, mismatchPenalty);
					}
					if (bwAln.alignmentLength >= minAlnLength && bwAln.alignmentIdentity >= minAlnIdentity)
					{
						bwAln.rightStart = paths[i].position.size() - 1 - bwAln.rightStart;
						bwAln.rightEnd = paths[i].position.size() - 1 - bwAln.rightEnd;
						std::swap(bwAln.rightStart, bwAln.rightEnd);
						for (size_t k = 0; k < bwAln.alignedPairs.size(); k++)
						{
							bwAln.alignedPairs[k].leftReverse = false;
							bwAln.alignedPairs[k].rightReverse = true;
							bwAln.alignedPairs[k].rightIndex = paths[i].position.size() - 1 - bwAln.alignedPairs[k].rightIndex;
						}
						bwAln.rightReverse = true;
						writequeue.enqueue(bwAln);
					}
				}
			}
		});
	}
	for (size_t i = 0; i < numThreads; i++)
	{
		threads[i].join();
	}
	overlapsFinished = true;
	overlapWriter.join();
	std::cerr << alnCount << " induced alignments" << std::endl;
}

int main(int argc, char** argv)
{
	std::string inputGraph { argv[1] };
	std::string inputAlns { argv[2] };
	size_t minAlnLength = std::stol(argv[3]);
	double minAlnIdentity = std::stod(argv[4]);
	int numThreads = std::stoi(argv[5]);
	std::string outputOverlaps { argv[6] };

	double mismatchPenalty = 10000;
	if (minAlnIdentity < 1.0)
	{
		mismatchPenalty = 1.0 / (1.0 - minAlnIdentity);
	}

	std::unordered_map<int, size_t> nodeSizes;
	{
		std::cerr << "load graph" << std::endl;
		auto graph = GfaGraph::LoadFromFile(inputGraph);
		graph.confirmDoublesidedEdges();
		nodeSizes = getNodeSizes(graph);
	}
	std::cerr << "load paths" << std::endl;
	auto paths = loadAlignmentsAsPaths(inputAlns, 1000, nodeSizes);
	std::cerr << paths.size() << " paths after filtering by length" << std::endl;
	std::cerr << "induce overlaps" << std::endl;
	induceOverlaps(paths, nodeSizes, mismatchPenalty, minAlnLength, minAlnIdentity, numThreads, outputOverlaps);
}