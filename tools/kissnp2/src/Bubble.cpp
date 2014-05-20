/*****************************************************************************
 *   GATB : Genome Assembly Tool Box
 *   Copyright (C) 2014  INRIA
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include <Bubble.hpp>
#include <Filter.hpp>
#include <Kissnp2.hpp>

using namespace std;

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
BubbleFinder::BubbleFinder (Kissnp2& tool, const Graph& graph)
    : tool(tool), graph(graph), sizeKmer(graph.getKmerSize()),
      _terminator(0), _traversal(0)
{
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
BubbleFinder::BubbleFinder (const BubbleFinder& bf)
    : tool(bf.tool), graph(bf.graph), sizeKmer(bf.graph.getKmerSize()),
      _terminator(0), _traversal(0)
{
    setTerminator (new BranchingTerminator(graph));
    setTraversal  (Traversal::create (tool.traversalKind, graph, *_terminator));
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
BubbleFinder::~BubbleFinder ()
{
    setTerminator (0);
    setTraversal  (0);
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
void BubbleFinder::operator() (const Node& node)
{
    /** We start the SNP in both directions (forward and reverse). */
    start (bubble, node);
    start (bubble, graph.reverse(node));
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
void BubbleFinder::start (Bubble& bubble, const Node& node)
{
    /** We get the mutations of the given node at position sizeKmer-1.
     * IMPORTANT: the third argument (set to 1) tells that the allowed nucleotide
     * variants must be greater than the nucleotide at position (sizeKmer-1) of the given node.
     * => We try all the possible extensions that were not previously tested (clever :-)) */
    Graph::Vector<Node> mutations = graph.mutate (node, sizeKmer-1, 1);

    /** We initialize the first path of the bubble. */
    bubble.begin[0] = node;

    /** We loop over all mutations. */
    for (size_t i=0; i<mutations.size(); i++)
    {
        /** We initialize the second path of the bubble. */
        bubble.begin[1] = mutations[i];

        /** We try to expand this new putative bubble. */
        expand (1, bubble, node, mutations[i], Node(~0), Node(~0));
    }
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
void BubbleFinder::expand (
    int pos,
    Bubble& bubble,
    const Node& node1,
    const Node& node2,
    const Node& previousNode1,
    const Node& previousNode2
)
{
    /** A little check won't hurt. */
    assert (pos <= sizeKmer-1);

    /** We may have to stop the extension according to the branching mode. */
    if (checkBranching(node1,node2) == false)  { return; }

    /** We get the common successors of node1 and node2. */
    Graph::Vector < pair<Node,Node> > successors = graph.successors<Node> (node1, node2);

    /** We loop over the successors of the two nodes. */
    for (size_t i=0; i<successors.size(); i++)
    {
        /** Shortcuts. */
        Node& nextNode1 = successors[i].first;
        Node& nextNode2 = successors[i].second;

        /** We check whether the new nodes are different from previous ones. */
        bool checkPrevious =
            checkNodesDiff (previousNode1, node1, nextNode1) &&
            checkNodesDiff (previousNode2, node2, nextNode2);

        if (!checkPrevious)  { continue; }

        /************************************************************/
        /**                   RECURSION CONTINUES                  **/
        /************************************************************/
        if (pos < sizeKmer-1)
        {
            /** We call recursively the method (recursion on 'pos'). */
            expand (pos+1, bubble,  nextNode1, nextNode2,  node1, node2);

            /** There's only one branch to expand if we keep non branching SNPs only, therefore we can safely stop the for loop */
            if ( tool.authorised_branching==0 || tool.authorised_branching==1 )   {  break;  }
        }

        /************************************************************/
        /**                   RECURSION FINISHED                   **/
        /************************************************************/
        else
        {
            /** We check the branching properties of the next kmers. */
            if (checkBranching(nextNode1, nextNode2)==false)  { return; }

            /** We finish the bubble with both last nodes. */
            bubble.end[0] = nextNode1;
            bubble.end[1] = nextNode2;

            /** We check several conditions (the first path vs. its revcomp and low complexity). */
            if (checkPath(bubble)==true && checkLowComplexity(bubble)==true)
            {
                /** We extend the bubble on the left and right. */
                if (extend (bubble) == true)
                {
                    /** We got all the information about the bubble, we finish it. */
                    finish (bubble);
                }
            }
        }

    } /* end of for (size_t i=0; i<successors.size(); i++) */
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
bool BubbleFinder::extend (Bubble& bubble)
{
    static const int BAD = -1;

    int closureLeft  = BAD;
    int closureRight = BAD;

    /** We may have to extend the bubble according to the user choice. */
    if (tool.traversalKind != Traversal::NONE)
    {
        /** We ask for the predecessors of the first node and successors of the last node. */
        Graph::Vector<Node> predecessors = graph.predecessors<Node> (bubble.begin[0]);
        Graph::Vector<Node> successors   = graph.successors<Node>   (bubble.end[0]);

        /** If unique, we keep the left/right extensions. */
        if (predecessors.size()==1)  { closureLeft  = graph.getNT (predecessors[0], 0);           }
        if (successors.size()  ==1)  { closureRight = graph.getNT (successors  [0], sizeKmer-1);  }

        /** We need to reset branching nodes between extensions in case of overlapping extensions. */
        _terminator->reset ();

        /** We compute right extension of the node. */
        _traversal->traverse (successors[0], DIR_OUTCOMING, bubble.extensionRight);
        bubble.divergenceRight = _traversal->getBubbles().empty() ? bubble.extensionRight.size() : _traversal->getBubbles()[0].first;

        /** We compute left extension of the node. */
        _traversal->traverse (graph.reverse(predecessors[0]), DIR_OUTCOMING, bubble.extensionLeft);
        bubble.divergenceLeft = _traversal->getBubbles().empty() ? bubble.extensionLeft.size() : _traversal->getBubbles()[0].first;
    }

    /** We return a code value according to left/right extensions status. */
         if (closureLeft==BAD && closureRight==BAD)  { bubble.where_to_extend = 0; }
    else if (closureLeft!=BAD && closureRight==BAD)  { bubble.where_to_extend = 1; }
    else if (closureLeft==BAD && closureRight!=BAD)  { bubble.where_to_extend = 2; }
    else if (closureLeft!=BAD && closureRight!=BAD)  { bubble.where_to_extend = 3; }

    bubble.closureLeft  = closureLeft;
    bubble.closureRight = closureRight;

    return true;
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
void BubbleFinder::finish (Bubble& bubble)
{
    /** We set the bubble index. NOTE: We have to make sure it is protected against concurrent
     * accesses since we may be called here from different threads. */
    bubble.index = __sync_add_and_fetch (&(tool.nb_bubbles), 1);

    /** We build two Sequence objects from the information of the bubble. */
    buildSequence (bubble, 0, "higher", bubble.seq1);
    buildSequence (bubble, 1, "lower",  bubble.seq2);

    /** We have to protect the sequences dump wrt concurrent accesses. We use a {} block with
     * a LocalSynchronizer instance with the shared ISynchonizer of the Kissnp2 class. */
    {
        LocalSynchronizer sync (tool._synchronizer);

        /** We insert the two sequences into the output bank. */
        tool._outputBank->insert (bubble.seq1);
        tool._outputBank->insert (bubble.seq2);

        /** Stats update (in concurrent access protection block). */
        tool.nb_where_to_extend[bubble.where_to_extend] ++;

        if (bubble.score < tool.threshold)  { tool.nb_bubbles_high++; }
        else                                { tool.nb_bubbles_low++;  }
    }
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
bool BubbleFinder::two_possible_extensions_on_one_path (const Node& node) const
{
    return graph.indegree(node)>=2 || graph.outdegree(node)>=2;
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
bool BubbleFinder::two_possible_extensions (Node node1, Node node2) const
{
    return
        graph.successors<Edge> (node1, node2).size() >= 2  ||
        graph.successors<Edge> (graph.reverse (node1),graph.reverse (node2)).size() >= 2;
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
void BubbleFinder::buildSequence (Bubble& bubble, size_t pathIdx, const char* type, Sequence& seq)
{
    stringstream commentStream;

    /** We build the comment for the sequence. */
    commentStream << "SNP_" << type << "_path_" << bubble.index << "|" << (bubble.score >= tool.threshold ? "low" : "high");

    /** We may have extra information for the comment. */
    if (tool.traversalKind == Traversal::UNITIG)
    {
        commentStream << "|left_unitig_length_";
        commentStream << (bubble.where_to_extend%2==1 ? (bubble.extensionLeft.size() +1) : 0);
        commentStream << "|right_unitig_length_";
        commentStream << (bubble.where_to_extend>1    ? (bubble.extensionRight.size()+1) : 0);
    }
    if (tool.traversalKind == Traversal::CONTIG)
    {
        commentStream << "|left_unitig_length_";
        commentStream << (bubble.where_to_extend%2==1 ? (bubble.divergenceLeft +1) : 0);
        commentStream << "|right_unitig_length_";
        commentStream << (bubble.where_to_extend>1    ? (bubble.divergenceRight+1) : 0);

        commentStream << "|left_contig_length_";
        commentStream << (bubble.where_to_extend%2==1 ? (bubble.extensionLeft.size() +1) : 0);
        commentStream << "|right_contig_length_";
        commentStream << (bubble.where_to_extend>1    ? (bubble.extensionRight.size()+1) : 0);
    }

    /** We assign the comment of the sequence. */
    seq.setComment (commentStream.str());

    static const int BAD = -1;

    size_t len = (2*sizeKmer-1) + bubble.extensionLeft.size() + bubble.extensionRight.size();

    if (bubble.closureLeft  != BAD)  { len++; }
    if (bubble.closureRight != BAD)  { len++; }

    /** We resize the sequence data if needed. Note: +1 for ending '\0'*/
    if (seq.getData().size() < len+1)  {  seq.getData().resize (len+1); }

    char* output = seq.getDataBuffer();

    size_t lenLeft  = bubble.extensionLeft.size ();
    for (size_t i=0; i<lenLeft; i++)  {  *(output++) = tolower(ascii (reverse(bubble.extensionLeft [lenLeft-i-1])));  }

    /** We add the left extension if any. Note that we use lower case for extensions. */
    if (bubble.closureLeft != BAD)   {  *(output++) = tolower(bin2NT[bubble.closureLeft]);  }

    /** We add the bubble path. */
    string begin = graph.toString (bubble.begin[pathIdx]);
    string end   = graph.toString (bubble.end[pathIdx]);

    for (size_t i=0; i<sizeKmer-1; i++)  {  *(output++) = begin[i];  }
    for (size_t i=0; i<sizeKmer; i++)    {  *(output++) = end  [i];  }

    /** We add the right extension if any. Note that we use lower case for extensions. */
    if (bubble.closureRight != BAD)  {  *(output++) =  tolower(bin2NT[bubble.closureRight]);  }

    size_t lenRight  = bubble.extensionRight.size ();
    for (size_t i=0; i<lenRight; i++)  {  *(output++) = tolower(ascii (bubble.extensionRight[i]));  }

    /** We add a null terminator for the strings. */
    *(output++) = '\0';
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
bool BubbleFinder::checkNodesDiff (const Node& previous, const Node& current, const Node& next) const
{
    return (next.kmer != current.kmer) && (next.kmer != previous.kmer);
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
bool BubbleFinder::checkPath (Bubble& bubble) const
{
    /** We test whether the first kmer of the first path is smaller than
     * the first kmer of the revcomp(first path), this should avoid repeated SNPs */
    return graph.toString (bubble.begin[0])  <  graph.toString (graph.reverse(bubble.end[0]));
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
bool BubbleFinder::checkBranching (const Node& node1, const Node& node2) const
{
    // stop the extension if authorised_branching==0 (not branching in any path) and any of the two paths is branching
    if (tool.authorised_branching==0 && (two_possible_extensions_on_one_path(node1) || two_possible_extensions_on_one_path(node2)))
    {
        return false;
    }

    // stop the extension if authorised_branching==1 (not branching in both path) and both the two paths are branching
    if (tool.authorised_branching==1 && two_possible_extensions (node1, node2))
    {
        return false;
    }

    return true;
}

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
bool BubbleFinder::checkLowComplexity (Bubble& bubble) const
{
    string path1 = graph.toString (bubble.begin[0]).substr(0, sizeKmer-1) + graph.toString (bubble.end[0]);
    string path2 = graph.toString (bubble.begin[1]).substr(0, sizeKmer-1) + graph.toString (bubble.end[1]);

    /** We compute the low complexity score of the two paths. */
    bubble.score = filterLowComplexity2Paths (path1, path2);

    return (bubble.score < tool.threshold || (bubble.score>=tool.threshold && tool.low));
}

